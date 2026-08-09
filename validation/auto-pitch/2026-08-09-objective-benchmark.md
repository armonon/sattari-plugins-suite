# Auto Pitch objective benchmark — 2026-08-09

Truth label: **machine benchmark passed with two realtime CPU warnings; real-vocal listening gate remains open**.

Command: `build/auto-pitch-neural/AutoPitchBench`

| Area | Result | Gate |
|---|---:|---|
| Correction | mean 0.01 cents; max 0.04 cents | pass |
| Latency | 2,048 samples reported; 2,080 measured; 0.67 ms delta | pass |
| Steady sine distortion | 0.0000% measured THD | pass |
| Rubber Band realtime CPU | RTF 0.397; p95 8.209 ms | pass |
| Studio lock realtime CPU | RTF 0.607; p95 19.873 ms | warning |
| Studio + Neural realtime CPU | RTF 0.632; p95 18.631 ms | warning |
| CREPE model | embedded and loaded | pass |
| Transitions | monotonic; 5.3 ms block spread | pass |
| Vibrato | 82% retained; synthetic depth/rate within bounds | pass |
| Polyphonic stress | 96–98% target agreement | pass |
| Safety | 83 renders; peak 0.9955; all finite | pass |

## Interpretation

The pitch target math, delay reporting, model loading, safety boundaries, and
synthetic transition behavior are strong enough for internal testing. The two p95
CPU warnings mean Studio modes should not be marketed as universally low-latency
until they pass the supported machine/buffer matrix. The sine THD result does not
measure real vocal artifacts such as consonant smearing, formant drift, transient
misclassification, breath pumping, or note-change lag.

## Open release gates

1. Run `scripts/qualify_auto_pitch_corpus.py` on at least five owned/cleared real vocal clips covering the manifest tags.
2. Complete the generated blinded or level-matched listening sheet.
3. Repeat realtime CPU and dropout tests at 44.1/48/96 kHz and 64/128/256/512 samples on Apple Silicon.
4. Do not claim Antares-equivalent vocal quality until the corpus listening gate and DAW matrix pass.
