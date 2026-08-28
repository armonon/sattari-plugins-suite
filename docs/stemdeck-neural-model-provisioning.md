# StemDeck neural stem separation — model provisioning (truth-labeled)

StemDeck can produce real 4-stem separation (drums / bass / music / vocals) two ways.
This document records what actually works today, what is blocked and *why*, and the exact
path to unblock the blocked route. Keep it honest.

## TL;DR

| Route | State | Notes |
| --- | --- | --- |
| **Demucs CLI (offline separator)** | ✅ **works, verified** | Full-quality htdemucs, out-of-process. StemDeck auto-detects it. |
| **In-process ONNX / CoreML** | ⛔ **blocked** | htdemucs uses complex-valued STFT/ISTFT, which neither PyTorch ONNX exporter supports. Needs a real-op STFT reimplementation (scoped below). |

**Recommendation:** the CLI route is the shippable real-separation path. The in-process
ONNX route is a latency/dependency optimization, **not** a prerequisite for real stems.

## Route A — Demucs CLI (works today)

`StemProvider` (`plugins/stemdeck/src/StemProvider.cpp`) shells out to a separator command
and collects the stems. It resolves the command in this order:

1. env `SATTARI_STEMDECK_SEPARATOR_CMD`
2. `~/Library/Sattari/StemDeck/StemProvider/separator-command.txt`
3. **auto-detect** a `demucs` binary at `~/Library/Python/3.{9,10,11}/bin/demucs`,
   `/usr/local/bin`, `/opt/homebrew/bin`, or `which demucs` — forming
   `<demucs> [-n <model>] --out {output} {input}`.

`{input}`/`{output}` are substituted; demucs writes `{output}/<model>/<track>/<stem>.wav`,
and `runSeparatorCommand` gathers `drums/bass/vocals.wav` plus `other.wav` (renamed to
`music.wav`). An optional `separator-model.txt` (e.g. `htdemucs_ft`) is passed via `-n`.

### Verified

Run mirroring StemProvider's exact command on a 10 s clip
(`validation/giantsteps/wav/3661550.wav`, 0:20–0:30), durable
`~/Library/Python/3.9/bin/demucs`, CPU:

```
demucs --out <dir> clip10.wav        # default model: htdemucs
→ <dir>/htdemucs/clip10/{drums,bass,other,vocals}.wav   (7.0 s wall)

drums   rms=0.1670   bass   rms=0.0978   other rms=0.0359   vocals rms=0.0611
sum(stems) vs mix   relative reconstruction error = 0.037   (near-lossless decomposition)
corr(drums,bass)=+0.011  corr(drums,other)=+0.014  corr(drums,vocals)=+0.043
corr(bass,other)=+0.025  corr(bass,vocals)=+0.006   corr(other,vocals)=+0.368
```

Low pairwise correlation ⇒ genuinely separated (not copies); 3.7 % reconstruction error ⇒
the four stems really do sum back to the mix. This is the real, full-quality htdemucs.

### Durable install (if `demucs` is ever missing)

```bash
python3 -m pip install --user demucs          # lands in ~/Library/Python/3.x/bin/demucs
# → StemProvider auto-detects it; no config needed.
```

(~2 GB with torch. htdemucs runs comfortably on CPU; `-d mps` uses the GPU but demucs
routes the STFT through CPU anyway. `htdemucs_ft` is ~4× slower for a small quality gain.)

## Route B — in-process ONNX (blocked, with the exact reason)

`NeuralStemEngine` (`plugins/stemdeck/src/NeuralStemEngine.cpp`) runs a self-contained
ONNX model through ONNX Runtime (CoreML EP) with this contract:

```
input  : float32 [1, 2, N]      stereo waveform, N = fixed model window (read from shape.back())
output : float32 [1, 4, 2, N]   4 stems (drums, bass, other, vocals), stereo
```

htdemucs's forward is exactly `[1,2,N] → [1,4,2,N]` (verified: torch forward returns
`(1,4,2,264600)` for a 6 s window), so the *contract* matches. Export is what fails.

### What was tried (`scripts/export_demucs_onnx.py`)

1. **`torch.onnx.export(dynamo=True, opset 18)`** — first blocked on a data-dependent
   `assert (...).all()` in `demucs/hdemucs.py:39 pad1d` (torch.export can't trace runtime
   asserts). Neutralized by monkeypatching `pad1d` to the identical reflect-pad math minus
   the sanity asserts. Then blocked again — the real wall:
   `DispatchError: No ONNX function found for aten.pad on complex-valued input`
   (demucs pads the **complex** spectrogram in `spec.py`).
2. **`torch.jit.trace` + `torch.onnx.export(dynamo=False, opset 17)`** (legacy exporter,
   the historically-proven Demucs path) — `SymbolicValueError: STFT does not currently
   support complex types`.
3. **`torch.onnx.export(dynamo=False, opset 17)` direct** — same STFT-complex error.

### Root cause

`demucs/spec.py` `spectro`/`ispectro` use `torch.stft(..., return_complex=True)` and
`torch.istft`, and `htdemucs._magnitude`/`_mask` bridge via `view_as_real`/`view_as_complex`.
**No PyTorch→ONNX exporter (dynamo or legacy/opset-17) supports complex-typed tensors
through STFT/ISTFT.** This is a known htdemucs↔ONNX limitation, not a bug in our script.

### Path to unblock (scoped, not yet done)

The complex band is narrow and fully identified: the complex tensor exists **only** between
`stft → view_as_real` (in `_magnitude`) and `view_as_complex → istft` (feeding `_ispec`);
with htdemucs's default complex-as-channels, `_mask` ignores the complex `z`. So a monkeypatch
of four functions makes the graph all-real and exportable:

- `spec.spectro` → real STFT via framed `unfold` + windowed matmul against precomputed
  DFT cos/sin bases (`normalized=True`, `center=True` reflect pad), returning **(re, im)
  stacked** instead of complex.
- `spec.ispectro` → real ISTFT: inverse-DFT matmul (interior rfft bins ×2) → window →
  overlap-add (`fold`/`conv_transpose1d`) → divide by window² envelope → trim to `length`.
- `htdemucs._magnitude` / `_ispec` (and the `_mask` cac branch) → consume/produce the
  real stacked layout instead of `view_as_real`/`view_as_complex`.

**Must be validated numerically against the reference model** (patched vs original output on
the same input, match to ~1e-3) before shipping — a wrong ISTFT yields plausible-RMS but
phase-wrong stems that a naive smoke test would pass. `plugins/stemdeck/tests/
stemdeck_neural_smoke.cpp` is the end-to-end C++ verifier once a model is installed to
`~/Library/Sattari/StemDeck/StemProvider/neural-model.onnx` (or `$SATTARI_STEMDECK_NEURAL_MODEL`).

Given Route A already delivers real, full-quality separation, Route B is a deferred
optimization (no Python dep, CoreML GPU, tighter packaging), tracked here rather than forced.
