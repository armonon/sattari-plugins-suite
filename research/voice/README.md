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
    tests/exp3_*.py   transposition range sweep, -24 to +24 semitones
    tests/exp4_*.py   nasality + moving tract: where the assumption breaks

## Chosen envelope method: LPC — with one caveat

Adaptive order (2/3 of the available harmonics) and pre-emphasis 0.5. Both of
those numbers came out of Experiment 1 and neither is the textbook value; see
`lpc_order_for()` for the sweep that forced the change.

Experiment 4 qualifies this: LPC is all-pole, and nasality adds a ZERO. On a
heavily nasal vowel LPC degrades most of the four (+1.16 dB) while the methods
that assume no pole structure barely move (+0.15 to +0.45), and PeakInterp ends
up ahead. LPC still wins overall, but "best on clean vowels" and "best on nasal
ones" are not the same method — a per-voice choice is a real option later.

## Playable range

Measured, not assumed. Spectral error against the true throat stays under 3 dB
across the whole -24..+24 sweep for all three source pitches — transposition
distance turns out NOT to be the limiting factor. SOURCE PITCH is:

    bass source (110 Hz)     0.56 dB mean      9% of the resampling control
    alto source (220 Hz)     0.89 dB          15%
    soprano source (440 Hz)  1.86 dB          26%

A soprano recording makes a worse instrument at EVERY pitch, including its own.
That is a recording-guidance finding, not a synthesis one: capture low.

## Where it stands

Octave down, envelope distance from the source throat: **0.12 dB** through the
model, **8.92 dB** through naive resampling. Octave up is 3.38 dB — worse, and
honestly so: at 330 Hz the harmonics are 330 Hz apart and the envelope is
sampled half as often.

Nasality and a moving tract are now tested (Experiment 4) and the failures are
graceful: a tract sliding 50% across the note drops confidence to `estimated`
and raises a warning naming the drift in dB.

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
