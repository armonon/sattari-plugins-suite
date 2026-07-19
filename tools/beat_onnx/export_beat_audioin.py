#!/usr/bin/env python3
"""Export beat_this (pretrained 'final0') to an AUDIO-IN ONNX for StemDeck.

beat_this's native front-end uses torch.stft, which the ONNX exporter rejects
("STFT does not currently support complex types" — same wall Demucs hit). So the
log-mel front-end is reproduced here with a REAL-valued conv-STFT (no complex
ops), matching torchaudio's MelSpectrogram exactly: Hann window, center reflect
pad, magnitude (power=1), frame_length normalisation (/win_length), the model's
own 513x128 slaney filterbank, then log1p(1000*mel).

Because the mel lives in the graph, the C++ side only has to resample to 22.05 kHz
and feed mono samples — no mel reimplementation, no parity risk.

The script asserts the conv-mel matches torchaudio's LogMelSpect numerically
BEFORE exporting, and asserts the exported ONNX matches the torch model, so a
green run means the .onnx is faithful.

  PYTHONPATH=<beat_this clone> python export_beat_audioin.py --out beat-model.onnx
"""
import argparse
import math
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

SR = 22050
N_FFT = 1024
HOP = 441
LOG_MULT = 1000.0


class ConvMel(nn.Module):
    """torchaudio MelSpectrogram(power=1, normalized='frame_length') + log1p(1000x),
    via a real conv-STFT so it is ONNX-exportable."""
    def __init__(self, window: torch.Tensor, fb: torch.Tensor):
        super().__init__()
        n = N_FFT
        k = torch.arange(n).float()
        f = torch.arange(n // 2 + 1).float().unsqueeze(1)
        ang = 2.0 * math.pi * f * k.unsqueeze(0) / n
        # analysis kernels [F, 1, n_fft] = window * cos/sin
        self.register_buffer("cos_k", (torch.cos(ang) * window).unsqueeze(1))
        self.register_buffer("sin_k", (torch.sin(ang) * window).unsqueeze(1))
        self.register_buffer("fb", fb)                 # [513, 128]
        self.pad = n // 2

    def forward(self, audio):                          # audio: [samples]
        x = audio.view(1, 1, -1)
        x = F.pad(x, (self.pad, self.pad), mode="reflect")   # center=True
        re = F.conv1d(x, self.cos_k, stride=HOP)[0]    # [F, T]
        im = -F.conv1d(x, self.sin_k, stride=HOP)[0]   # sign: e^{-j...}
        # power=1 (magnitude), normalized='frame_length' => divide by sqrt(win_length)
        mag = torch.sqrt(re * re + im * im + 1e-20) / math.sqrt(N_FFT)
        mel = torch.matmul(self.fb.transpose(0, 1), mag)     # [128, T]
        return torch.log1p(LOG_MULT * mel).transpose(0, 1)   # -> [T, 128]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="beat-model.onnx")
    ap.add_argument("--checkpoint", default="final0")
    args = ap.parse_args()

    from beat_this.inference import load_model
    from beat_this.preprocessing import LogMelSpect

    ref = LogMelSpect().eval()
    window = ref.spect_class.spectrogram.window.detach().clone()
    fb = ref.spect_class.mel_scale.fb.detach().clone()

    conv = ConvMel(window, fb).eval()

    # ── Parity gate 1: conv-mel vs torchaudio on real-ish audio ──────────────
    torch.manual_seed(0)
    audio = torch.randn(SR * 5) * 0.1
    with torch.no_grad():
        a = ref(audio)                         # LogMelSpect returns (T, 128)
        b = conv(audio)                        # (T, 128)
    T = min(a.shape[0], b.shape[0])
    d = (a[:T] - b[:T]).abs().max().item()
    print(f"[parity] conv-mel vs torchaudio max abs diff = {d:.3e}")
    assert d < 2e-3, f"mel mismatch {d} — fix normalisation before exporting"

    model = load_model(args.checkpoint, device="cpu")

    class AudioIn(nn.Module):
        def __init__(self):
            super().__init__(); self.mel = conv; self.m = model
        def forward(self, audio):
            out = self.m(self.mel(audio).unsqueeze(0))
            return torch.sigmoid(out["beat"]), torch.sigmoid(out["downbeat"])

    net = AudioIn().eval()

    # ── Export ───────────────────────────────────────────────────────────────
    dummy = torch.randn(SR * 5) * 0.1
    torch.onnx.export(
        net, dummy, args.out,
        input_names=["audio"], output_names=["beat", "down"],
        dynamic_axes={"audio": {0: "samples"}, "beat": {1: "frames"}, "down": {1: "frames"}},
        opset_version=17, do_constant_folding=True)

    # ── Parity gate 2: ONNX vs torch ─────────────────────────────────────────
    import onnxruntime as ort
    with torch.no_grad():
        tb, td = net(dummy)
    s = ort.InferenceSession(args.out, providers=["CPUExecutionProvider"])
    ob, od = s.run(["beat", "down"], {"audio": dummy.numpy()})
    eb = np.abs(tb.numpy() - ob).max()
    ed = np.abs(td.numpy() - od).max()
    print(f"[parity] ONNX vs torch: beat {eb:.3e}  down {ed:.3e}")
    assert eb < 1e-4 and ed < 1e-4, "ONNX diverged from torch"

    print(f"OK wrote {args.out}  (audio-in @ {SR} Hz, ~{HOP/SR*1000:.1f} ms/frame)")


if __name__ == "__main__":
    main()
