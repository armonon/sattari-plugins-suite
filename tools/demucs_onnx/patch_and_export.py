#!/usr/bin/env python3
"""Monkeypatch htdemucs's spectral branch to a real-valued, ONNX-exportable
STFT/ISTFT built from CONSTANT buffers (conv kernels + iDFT matrices), so no
trig/complex ops reach the graph. Validate vs original, then export.
Contract: [1,2,N] -> [1,4,2,N] (drums,bass,other,vocals) @44.1k."""
import sys, math, types, traceback
import torch
import torch.nn.functional as F
from demucs.pretrained import get_model
from demucs.htdemucs import HTDemucs

MODEL = sys.argv[1] if len(sys.argv) > 1 else "htdemucs"
OUT   = sys.argv[2] if len(sys.argv) > 2 else "neural-model.onnx"


def attach_buffers(m):
    nfft = m.nfft
    Ff = nfft // 2 + 1
    win = torch.hann_window(nfft, dtype=torch.float64)
    n = torch.arange(nfft, dtype=torch.float64)
    k = torch.arange(Ff, dtype=torch.float64)
    ang = -2.0 * math.pi / nfft * (k[:, None] * n[None, :])          # [F,nfft]
    norm = 1.0 / math.sqrt(nfft)
    m.register_buffer("_kre", ((torch.cos(ang) * win[None, :]) * norm).unsqueeze(1).float())  # [F,1,nfft]
    m.register_buffer("_kim", ((torch.sin(ang) * win[None, :]) * norm).unsqueeze(1).float())
    ang2 = 2.0 * math.pi / nfft * (n[:, None] * k[None, :])          # [nfft,F]
    wk = torch.full((Ff,), 2.0, dtype=torch.float64); wk[0] = 1.0; wk[-1] = 1.0
    m.register_buffer("_icosT", (torch.cos(ang2) * wk[None, :] / nfft).transpose(0, 1).float())  # [F,nfft]
    m.register_buffer("_isinT", (torch.sin(ang2) * wk[None, :] / nfft).transpose(0, 1).float())
    m.register_buffer("_synthwin", (win * math.sqrt(nfft)).float())  # [nfft]
    m.register_buffer("_eye", torch.eye(nfft).unsqueeze(1).float())  # [nfft,1,nfft]
    m.register_buffer("_win2", (win * win).reshape(1, 1, nfft).float())


def _spec_real(self, x):
    hl, nfft = self.hop_length, self.nfft
    le = int(math.ceil(x.shape[-1] / hl))
    pad = hl // 2 * 3
    x = F.pad(x, (pad, pad + le * hl - x.shape[-1]), mode="reflect")
    B, C, L = x.shape
    xp = F.pad(x.reshape(B * C, 1, L), (nfft // 2, nfft // 2), mode="reflect")
    re = F.conv1d(xp, self._kre, stride=hl)                         # [B*C,F,frames]
    im = F.conv1d(xp, self._kim, stride=hl)
    Ff = nfft // 2 + 1
    re = re.reshape(B, C, Ff, -1)[..., :-1, :][..., 2:2 + le]       # [B,C,2048,le]
    im = im.reshape(B, C, Ff, -1)[..., :-1, :][..., 2:2 + le]
    return torch.stack([re, im], dim=2)                            # [B,C,2,2048,le]


def _magnitude_real(self, z):
    B, C, two, Fr, T = z.shape
    return z.reshape(B, C * two, Fr, T)


def _mask_real(self, z, m):
    B, S, C2, Fr, T = m.shape
    return m.view(B, S, C2 // 2, 2, Fr, T)


def _ispec_real(self, z, length=None, scale=0):
    hl, nfft = self.hop_length // (4 ** scale), self.nfft
    z = F.pad(z, (0, 0, 0, 1))                                      # Fr 2048->2049
    z = F.pad(z, (2, 2))                                            # T +4
    pad = hl // 2 * 3
    le = hl * int(math.ceil(length / hl)) + 2 * pad
    B, S, C, two, Fr, T = z.shape
    re = z[:, :, :, 0].reshape(B * S * C, Fr, T).transpose(1, 2)    # [B2,T,F]
    im = z[:, :, :, 1].reshape(B * S * C, Fr, T).transpose(1, 2)
    frames = re @ self._icosT - im @ self._isinT                   # [B2,T,nfft]
    frames = frames * self._synthwin[None, None, :]
    fc = frames.transpose(1, 2)                                    # [B2,nfft,T]
    y = F.conv_transpose1d(fc, self._eye, stride=hl).squeeze(1)
    ones = torch.ones(fc.shape[0], 1, fc.shape[2], dtype=fc.dtype)
    ola = F.conv_transpose1d(ones, self._win2, stride=hl).squeeze(1)
    y = y / (ola + 1e-8)
    cpad = nfft // 2
    y = y[:, cpad:cpad + le].reshape(B, S, C, le)
    return y[..., pad:pad + length]


bag = get_model(MODEL)
sub = getattr(bag, "models", None)
model = sub[0] if sub else bag
model.eval()
assert isinstance(model, HTDemucs) and model.cac
sr, sources = int(model.samplerate), list(model.sources)
print(f"[info] {type(model).__name__} sr={sr} sources={sources} nfft={model.nfft} hop={model.hop_length}")

N = int(round(7.0 * sr / model.hop_length)) * model.hop_length
x = torch.randn(1, 2, N, dtype=torch.float32) * 0.15
with torch.no_grad():
    y_ref = model(x)

attach_buffers(model)
model._spec      = types.MethodType(_spec_real, model)
model._magnitude = types.MethodType(_magnitude_real, model)
model._mask      = types.MethodType(_mask_real, model)
model._ispec     = types.MethodType(_ispec_real, model)
with torch.no_grad():
    y_new = model(x)
diff = (y_ref - y_new).abs().max().item()
print(f"[patch] out {tuple(y_new.shape)} max|diff|={diff:.3e} rel={diff/(y_ref.abs().max().item()+1e-9):.3e}")
if diff > 5e-3:
    print("PATCH_FAIL"); sys.exit(2)
print("PATCH_OK")

class Wrap(torch.nn.Module):
    def __init__(self, m): super().__init__(); self.m = m
    def forward(self, x): return self.m(x)

wrap = Wrap(model).eval()
try:
    torch.onnx.export(wrap, x, OUT, input_names=["mix"], output_names=["stems"],
                      opset_version=17, do_constant_folding=True, dynamic_axes=None)
    print("[export] OK opset 17"); sys.exit(0)
except Exception as e:
    print(f"[export] FAILED: {type(e).__name__}: {str(e)[:200]}")
    traceback.print_exc(); sys.exit(1)
