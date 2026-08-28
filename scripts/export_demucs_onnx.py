#!/usr/bin/env python3
"""Export htdemucs (single model) to ONNX matching StemDeck's NeuralStemEngine contract.

Contract (plugins/stemdeck/src/NeuralStemEngine.cpp):
    input  : float32 [1, 2, N]         stereo waveform, N = fixed model window
    output : float32 [1, 4, 2, N]      4 stems (drums, bass, other, vocals), stereo

The plugin reads N from the model's input shape (shape.back()) and windows the source
with 25% overlap + Hann. So a FIXED-length export is exactly what it wants.

Two blockers on htdemucs export, both handled here:
  1. demucs/hdemucs.py pad1d() has a data-dependent `assert (...).all()` that torch.export
     cannot trace. We monkeypatch pad1d to the identical math minus the sanity asserts.
  2. torch.export (dynamo) is picky about the STFT/ISTFT; torch.jit.trace records the
     concrete op trace and is the historically-proven path for demucs -> ONNX. We try the
     dynamo path first (best op decompositions), then jit.trace + the legacy exporter.
"""
from __future__ import annotations
import sys, traceback
from pathlib import Path
import torch
import torch.nn.functional as F

OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("neural-model.onnx")
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0


def patch_pad1d():
    """Replace demucs pad1d with an assert-free clone (same reflect-pad math)."""
    import demucs.hdemucs as hd
    import demucs.htdemucs as ht

    def pad1d(x, paddings, mode="constant", value=0.0):
        length = x.shape[-1]
        padding_left, padding_right = paddings
        if mode == "reflect":
            max_pad = max(padding_left, padding_right)
            if length <= max_pad:
                extra_pad = max_pad - length + 1
                extra_pad_right = min(padding_right, extra_pad)
                extra_pad_left = extra_pad - extra_pad_right
                paddings = (padding_left - extra_pad_left, padding_right - extra_pad_right)
                x = F.pad(x, (extra_pad_left, extra_pad_right))
        return F.pad(x, paddings, mode, value)

    hd.pad1d = pad1d
    ht.pad1d = pad1d   # htdemucs.py did `from .hdemucs import pad1d` — rebind that too
    print("patched pad1d (asserts removed)", flush=True)


def build():
    from demucs.pretrained import get_model
    bag = get_model("htdemucs")
    model = bag.models[0].eval().cpu().float()
    sr = int(model.samplerate)
    n = int(round(SECONDS * sr))
    print(f"htdemucs single model; samplerate={sr}, window N={n} ({SECONDS:.1f}s)", flush=True)

    class Wrap(torch.nn.Module):
        def __init__(self, m):
            super().__init__(); self.m = m
        def forward(self, x):
            return self.m(x)

    wrap = Wrap(model).eval()
    dummy = torch.randn(1, 2, n, dtype=torch.float32)
    with torch.no_grad():
        y = wrap(dummy)
    print("torch forward OK, output shape:", tuple(y.shape), flush=True)
    assert y.shape[1:3] == (4, 2), y.shape
    return wrap, dummy, n


def main() -> int:
    patch_pad1d()
    wrap, dummy, n = build()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    errors = []

    # 1) dynamo exporter (best STFT decomposition), now that pad1d is assert-free.
    try:
        print("== attempt 1: torch.onnx.export(dynamo=True) ==", flush=True)
        torch.onnx.export(wrap, (dummy,), str(OUT),
                          input_names=["mix"], output_names=["stems"],
                          dynamo=True, opset_version=18)
        print("dynamo export wrote", OUT, flush=True)
        return verify(OUT, n)
    except Exception as e:
        errors.append(("dynamo", short(e))); traceback.print_exc()

    # 2) legacy TorchScript exporter over a concrete jit.trace (ignores Python asserts,
    #    proven path for demucs). opset 17 has native STFT.
    try:
        print("== attempt 2: jit.trace + torch.onnx.export(dynamo=False, opset 17) ==", flush=True)
        with torch.no_grad():
            traced = torch.jit.trace(wrap, dummy, check_trace=False)
        torch.onnx.export(traced, (dummy,), str(OUT),
                          input_names=["mix"], output_names=["stems"],
                          dynamo=False, opset_version=17, do_constant_folding=True)
        print("legacy export wrote", OUT, flush=True)
        return verify(OUT, n)
    except Exception as e:
        errors.append(("legacy-jit", short(e))); traceback.print_exc()

    # 3) legacy exporter directly (its own internal trace), opset 17.
    try:
        print("== attempt 3: torch.onnx.export(dynamo=False, opset 17) direct ==", flush=True)
        torch.onnx.export(wrap, (dummy,), str(OUT),
                          input_names=["mix"], output_names=["stems"],
                          dynamo=False, opset_version=17, do_constant_folding=True)
        print("legacy-direct export wrote", OUT, flush=True)
        return verify(OUT, n)
    except Exception as e:
        errors.append(("legacy-direct", short(e))); traceback.print_exc()

    print("\n==== EXPORT FAILED ====", flush=True)
    for tag, err in errors:
        print(f"[{tag}] {err}", flush=True)
    # Surface whether ISTFT is the wall (the one op with no clean ONNX equivalent).
    joined = " ".join(e for _, e in errors).lower()
    if "istft" in joined:
        print("\nDIAGNOSIS: torch.istft has no ONNX op — the decoder's inverse STFT is the wall.", flush=True)
    return 1


def short(e: Exception) -> str:
    s = repr(e)
    return s if len(s) < 400 else s[:400] + "…"


def verify(path: Path, n: int) -> int:
    print("== verify with onnxruntime ==", flush=True)
    import numpy as np, onnxruntime as ort
    so = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    iname = so.get_inputs()[0].name
    print("  onnx in:", iname, so.get_inputs()[0].shape,
          "out:", so.get_outputs()[0].shape, flush=True)
    x = np.random.randn(1, 2, n).astype(np.float32)
    out = so.run(None, {iname: x})[0]
    ok = out.ndim == 4 and out.shape[1] == 4 and out.shape[2] == 2
    print("  onnxruntime out shape:", out.shape, " CONTRACT MATCH:", ok, flush=True)
    print(f"  file size: {path.stat().st_size/1e6:.1f} MB", flush=True)
    return 0 if ok else 3


if __name__ == "__main__":
    raise SystemExit(main())
