# Voice → Instrument (research prototype)

Analysis → sound model → playable instrument. Python first, deliberately: the
DSP questions are answered here and the C++ port follows, the same way
`SattariLogSpec.h` has a Python twin held to 0.999995 parity.

    python3.11 -m venv .venv && .venv/bin/pip install numpy scipy soundfile matplotlib librosa
    .venv/bin/python tests/exp0_ground_truth.py

## Layout

    src/model.py      VoiceInstrumentModel — versioned, confidence on every field
    src/analysis.py   stages 1-3: preprocess, stable region, F0, harmonics
    tests/synth.py    synthetic vowels with KNOWN ground truth
    tests/exp0_*.py   analysis accuracy vs that ground truth

## Why Experiment 0 exists

On a real recording the only available verdict is "does that spectrogram look
about right?", and a subtly broken analyzer passes that every time. Here the true
F0, harmonic amplitudes and envelope are inputs, so error is a number.

It has already earned its keep — see the commit history for three bugs it caught
that a listening test would have blamed on the wrong stage.
