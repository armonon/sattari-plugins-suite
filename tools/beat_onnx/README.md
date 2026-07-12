# Neural beat/downbeat tracker → ONNX (StemDeck)

Upgrades StemDeck's beat detection from the built-in DSP pipeline (multiband
spectral-flux onsets → Ellis dynamic-programming tracker) to a **trained** beat +
downbeat tracker — the same class of model djay Pro uses, and the one remaining
algorithmic gap to it on *messy* material (live drummers, rubato, ambiguous
downbeats). The DSP pipeline stays as the fallback, so the app always works with
no model installed.

This mirrors `tools/demucs_onnx`: run the export once in your training env, commit
the `.onnx`, install it next to the separation model. No dataset needed to
*export* — the source models ship pretrained; the dataset was used by their
authors to train them.

## 1. Export

```bash
pip install "beat_this @ git+https://github.com/CPJKU/beat_this"   # or: pip install BeatNet
python export_beat.py --backend beat_this --out beat-model.onnx
```

`beat_this` (Foscarini/Böck 2024, transformer) is the recommended backend — it is
current SOTA on beat + downbeat F-measure. `beatnet` (CRNN) is a lighter option.
You may need to adjust the one checkpoint-loading line in `export_beat.py` to match
the installed package version (its API moves); the I/O contract below is what must
stay fixed.

## 2. Verify

```bash
pip install onnxruntime
python verify_beat_onnx.py beat-model.onnx --bpm 128
# -> true 128.0 BPM -> detected ~128 BPM (...)  BEAT_ONNX_OK
```

This synthesizes a click track, runs the graph, peak-picks the beat activation,
and checks the recovered tempo — proving the exported graph runs and localizes
beats. (Real-world F-measure comes from the pretrained weights; this just checks
the export.)

## 3. Install

```
~/Library/Sattari/StemDeck/StemProvider/beat-model.onnx
```
(Same directory convention as `neural-model.onnx`. Override with
`SATTARI_STEMDECK_BEAT_MODEL`.)

## I/O contract (keep stable — the C++ side depends on it)

| tensor | shape | meaning |
|--------|-------|---------|
| in  `mel`  | `float32 [1, frames, 128]` | log1p mel spectrogram |
| out `beat` | `float32 [1, frames]`      | per-frame beat activation 0..1 |
| out `down` | `float32 [1, frames]`      | per-frame downbeat activation 0..1 |

Frontend (the C++ mel extractor must match): `sr=22050, n_fft=1024, hop=441`
(~50 fps), `n_mels=128, fmin=30, fmax=11025`, `log1p` magnitude mel.

## C++ integration (the hook, once the model exists)

Add `NeuralBeatEngine` (parallel to `NeuralStemEngine`, compiled only when
`SATTARI_STEMDECK_HAS_ONNX`), then in the app's analysis orchestration — NOT in
`TrackAnalysis.cpp`, which must stay ORT-free so the light test targets keep
building:

1. Run `TrackAnalyzer::analyze` as today (gives tempo prior, key, energy).
2. If `NeuralBeatEngine::isAvailable()`: resample to 22.05 kHz, compute the log-mel
   above, run the model → `beat`/`down` activations.
3. Decode beats: peak-pick (or a DBN over the activation) → beat samples;
   downbeat = argmax of `down` within the metrically-correct phase. Fold to the
   analyzer's tempo octave using the existing perceptual prior.
4. Overwrite `analysis.beatGridSamples` / `downbeatSample` / `beatsPerBar` with the
   decoded result; keep the DSP result whenever the model is absent or its mean
   activation is below a confidence floor.

`StemDeckBeatDetectTest` already provides the ground-truth harness to re-verify the
decoded grid against synthetic tempos once the model is wired.
