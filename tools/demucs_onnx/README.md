# Demucs → ONNX for StemDeck's on-device neural separation

Produces `neural-model.onnx` matching `NeuralStemEngine`'s contract:
input `[1,2,N]` (stereo, de-interleaved L/R, 44.1kHz) → output `[1,4,2,N]`
(sources: drums, bass, other/music, vocals).

## Why this is non-trivial
htdemucs uses a complex-valued STFT (`torch.stft(return_complex=True)` + complex
`pad`), which **neither** the legacy nor the dynamo ONNX exporter can handle
("STFT does not currently support complex types"). `rstft.py` reimplements the
STFT/ISTFT with real-valued Conv1d / matmul that match `torch.stft`/`istft` to
~1e-6, and `patch_and_export.py` monkeypatches htdemucs's `_spec/_magnitude/
_mask/_ispec` to thread real re/im — the patched model matches the original to
~6e-7, then exports cleanly at opset 17.

## Regenerate
    pip install --user torch demucs onnx onnxscript onnxruntime
    python3 patch_and_export.py htdemucs neural-model.onnx   # ~370MB fp32
    python3 verify_onnx.py neural-model.onnx                 # sanity check

## Install (note the path!)
JUCE's `userApplicationDataDirectory` is `~/Library` on macOS (NOT
`~/Library/Application Support`), so the app loads the model from:
    ~/Library/Sattari/StemDeck/StemProvider/neural-model.onnx
(or override with env `SATTARI_STEMDECK_NEURAL_MODEL`).

## CPU vs CoreML
The engine defaults to CPU fp32. CoreML runs the graph in fp16 on the ANE/GPU,
which overflows the wide DFT reductions in the STFT/ISTFT and yields NaNs — so
CoreML is opt-in via env `SATTARI_STEMDECK_NEURAL_COREML=1` (only safe once
validated for a given model). ~100s for 20s of audio on CPU (Apple Silicon).
