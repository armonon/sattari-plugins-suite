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
    src/pipeline.py   stages 1-7 assembled: audio in, VoiceInstrumentModel out
    src/synthesis.py  play the model at any pitch (+ the resampling control)
    tests/synth.py    synthetic vowels with KNOWN ground truth
    tests/exp0_*.py   analysis accuracy vs that ground truth
    tests/exp1_*.py   envelope shootout — which method survives transposition
    tests/exp2_*.py   round trip + the transposition claim, against resampling

## Chosen envelope method: LPC

Adaptive order (2/3 of the available harmonics) and pre-emphasis 0.5. Both of
those numbers came out of Experiment 1 and neither is the textbook value; see
`lpc_order_for()` for the sweep that forced the change.

## Where it stands

Octave down, envelope distance from the source throat: **0.12 dB** through the
model, **8.92 dB** through naive resampling. Octave up is 3.38 dB — worse, and
honestly so: at 330 Hz the harmonics are 330 Hz apart and the envelope is
sampled half as often.

Everything so far is synthetic. The synthetic vowels obey the same source-filter
decomposition the analyzer assumes, so they test the ANALYSIS, not the
assumption. A real voice — nasality, anti-formants, a tract that moves — is the
next thing this needs and the one thing it has not had.

## Why Experiment 0 exists

On a real recording the only available verdict is "does that spectrogram look
about right?", and a subtly broken analyzer passes that every time. Here the true
F0, harmonic amplitudes and envelope are inputs, so error is a number.

It has already earned its keep — see the commit history for three bugs it caught
that a listening test would have blamed on the wrong stage.
