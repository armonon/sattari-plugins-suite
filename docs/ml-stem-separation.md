# ML stem separation for StemDeck (implemented engine / distribution plan)

Goal: bring **Moises / RipX-class** 4-stem separation (drums / bass / other / vocals)
into StemDeck, so a loaded track can be split into stems on import. This is an
**offline** feature — it runs once on a background thread when a track is loaded,
not in the realtime audio callback.

## What is implemented

- `NeuralStemEngine` and the shared separator code implement the waveform inference
  pipeline (normalise → segment → ONNX `Run` → overlap-add → de-normalise),
  progressive playhead-first segments, cancellation, and CPU/CoreML provider setup.
  The default build keeps this compiled out with **zero** ONNX dependency.
- `cmake/SattariOnnx.cmake` — `sattari_enable_onnx(target DEFINE)`, **OFF by
  default** (`-DSATTARI_USE_ONNX=ON` to enable). Mirrors `SattariRubberBand.cmake`.
- `plugins/stemdeck/CMakeLists.txt` — calls `sattari_enable_onnx(SattariStemDeck
  SATTARI_HAS_ONNX)`. With ONNX off it's a no-op, so the green build is untouched.

ONNX smoke and cancellation contracts exist when a compatible runtime/model are
configured. The remaining product step is choosing and licensing the exact weights
and deployable runtime, then executing the signed/notarized package on clean machines.

## Architecture (how it slots into StemDeck)

StemDeck already runs offline analysis on a background thread (`TrackAnalyzer`,
the key-map/beat-grid scan). Stem separation joins that path:

```
load track ─► background thread ─► StemSeparator.separate() ─► cache 4 stem files
                                                              └─► deck lanes play stems
```

The realtime engine is unchanged — it just gains 4 cached stem buffers to play,
mute, and re-pitch with the existing Rubber Band path.

## Model

- **Recommended:** `htdemucs` (Hybrid Transformer Demucs, 4-stem) exported to
  ONNX, or an **MDX-Net** model. Demucs gives the best quality/space trade for a
  4-stem split; MDX-Net is lighter.
- **Code license:** Demucs (MIT), ONNX Runtime (MIT) — both GPLv3-compatible.
- ⚠️ **Weights license is separate** — vet the exact checkpoint for commercial
  redistribution. Ship the model as a downloaded asset (first-run fetch) rather
  than bundling, to keep the binary small and the weights swappable.

## Inference pipeline (the work remaining in `separate()`)

1. **Resample** input to the model rate (Demucs = 44.1 kHz).
2. **Segment** into overlapping windows (htdemucs ≈ 7.8 s segments, ~25%
   overlap) so arbitrarily long tracks fit fixed-size model input.
3. Per segment, build the input tensor **`[1, channels, segLen]`**, `Run()` the
   session.
4. Model returns **`[1, numStems=4, channels, segLen]`**.
5. **Windowed overlap-add** each stem back into the output buffers (a Hann/
   triangular fade across the overlap removes seams).
6. Report progress per segment; write results into `StemResult.stems`.

## Engine

- **ONNX Runtime** (C++), via `cmake/SattariOnnx.cmake`.
- On Apple silicon, append the **CoreML execution provider** for a big speedup;
  CPU EP elsewhere. (DirectML on Windows is the equivalent.)

## How to try it locally

```bash
# 1. download an onnxruntime release, extract, point at it
export ONNXRUNTIME_ROOT=/path/to/onnxruntime-osx-arm64-1.x
# 2. configure with ONNX on
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Release -DSATTARI_USE_ONNX=ON
# 3. in plugins/stemdeck/CMakeLists.txt add, after sattari_add_plugin(...):
#       sattari_enable_onnx(SattariStemDeck SATTARI_HAS_ONNX)
# 4. obtain an htdemucs/MDX .onnx model, call StemSeparator::loadModel() from
#    the TrackAnalyzer thread, then separate() on import.
```

## Other ML wins (same engine, later)

- **CREPE / torchcrepe** pitch as an offline "high-accuracy" mode for Auto Pitch
  (key map) + Harmony + VoxKey — same ONNX Runtime backend.
- **RNNoise / DeepFilterNet** denoise for a vocal-cleanup utility.

The pattern is the rule: **ML offline for quality, DSP realtime for latency.**
