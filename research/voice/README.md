# Voice → Instrument (research prototype)

Analysis → sound model → playable instrument. Python first, deliberately: the
DSP questions are answered here and the C++ port follows, the same way
`SattariLogSpec.h` has a Python twin held to 0.999995 parity.

    python3.11 -m venv .venv && .venv/bin/pip install numpy scipy soundfile matplotlib librosa
    .venv/bin/python tests/exp0_ground_truth.py

## Layout

    src/model.py      VoiceInstrumentModel — versioned, confidence on every field
    src/analysis.py   stages 1-3: preprocess, stable region, F0, harmonics
    src/envelope.py   stage 4: four envelope methods (LPC / cepstral / true / peak)
    tests/synth.py    synthetic vowels with KNOWN ground truth
    tests/exp0_*.py   analysis accuracy vs that ground truth
    tests/exp1_*.py   envelope shootout — which method survives transposition

## Chosen envelope method: LPC

Adaptive order (2/3 of the available harmonics) and pre-emphasis 0.5. Both of
those numbers came out of Experiment 1 and neither is the textbook value; see
`lpc_order_for()` for the sweep that forced the change.

## Why Experiment 0 exists

On a real recording the only available verdict is "does that spectrogram look
about right?", and a subtly broken analyzer passes that every time. Here the true
F0, harmonic amplitudes and envelope are inputs, so error is a number.

It has already earned its keep — see the commit history for three bugs it caught
that a listening test would have blamed on the wrong stage.
