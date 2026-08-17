# Auto Pitch objective benchmark — 2026-08-09

Truth label: **machine benchmark passed; the two CPU warnings below were
measurement artifacts and are withdrawn. Real-vocal listening gate remains open**.

> **RC2 ADDENDUM 2026-08-16.** Auto Pitch 0.5.0 now explicitly uses Rubber Band
> R3/Finer. The new full benchmark passes correction, fixed and reconstructed-path
> latency, THD/alias proxy, CPU at the declared 48 kHz / 512-sample test setting,
> transitions, vibrato, polyphonic stress and safety: 100 renders, 0 violations,
> peak 0.9955. Host latency is 5,428 samples; forced-unity R3 differs by 0.54 ms
> and a genuinely tuned onset by 0.56 ms. Dry/zero-Amount nulls are `-inf` dBFS
> at zero offset. CPU p95 is 3.19 ms against a 10.67 ms block budget. The supported
> print-quality envelope remains conservative: 44.1/48 kHz at 256 samples or
> above; 64 samples and 96 kHz are not approved. Human listening remains open.

> **SUPERSEDED 2026-08-14 — the two "realtime CPU warning" rows.**
> The 19.873 ms and 18.631 ms p95 figures were **wall clock**, which counts time
> this thread spent preempted by everything else on the machine. Re-measured with
> `CLOCK_THREAD_CPUTIME_ID`, which counts only cycles actually executed:
>
> | path | was (wall p95) | is (CPU p95) | budget @ 48k/512 |
> |---|---:|---:|---:|
> | Rubber Band | 8.209 ms | **2.256 ms** | 10.67 ms |
> | Studio lock | 19.873 ms | **2.247 ms** | 10.67 ms |
> | Studio + Neural | 18.631 ms | **2.368 ms** | 10.67 ms |
>
> All three PASS with ~4.7x headroom. There was never a Studio-mode CPU problem;
> there was a stopwatch that counted other programs' time as ours. Open gate 3
> from this document ("repeat realtime CPU tests across the rate/buffer matrix")
> is **done** — the matrix runs in `AutoPitchBench cpu`, and the only real limits
> are 96 kHz at <=128 samples (FAIL) and 64 samples at any rate (WARN), which is
> exactly the documented 44.1/48 kHz at >=128 envelope.

> **SUPERSEDED 2026-08-14 — the latency `pass` row below.** The measured
> 2,080-sample output against a 2,048-sample report is not harmless: host PDC is
> permanently 32 samples wrong and the automated null test reads -1.0 dBFS at
> the reported alignment. It is now a Gate 4 blocker; see
> `validation/auto-pitch/2026-08-14-gate4-null-and-engine-lock.md`.

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
