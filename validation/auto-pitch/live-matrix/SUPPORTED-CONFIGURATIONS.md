# Auto Pitch: supported configurations for claims

Evidence: `2026-09-27-e94d5e7.md` / `.json` (AutoPitchLiveMatrix, DSP identical to `a87e261`).
This file lists only what that evidence supports. Update it only from a newer matrix run, never by
hand-editing a cell. "Supported" means both verdicts pass:

- **Matrix verdict:** CPU p99 <= 70 % and max <= 100 % of the block budget; latency honest and
  constant; accuracy, vibrato and settling within their gates; no dropouts or NaN.
- **Artifact verdict:** no waveform discontinuity >= -20 dB re peak on tones or vocal, and none
  of 12 note changes clicks at that level.

Limits of the evidence:
- Accuracy and artifacts use synthetic material. Timing was also checked on 6 real vocals
  (vocadito dev).
- CPU is one Apple M4 P-core with hot caches, one instance: a best case.
- Every "supported" cell still needs DAW and listening confirmation before a public claim.

## Studio (print engine)

| SR | Supported buffers | Not supported | Why not |
|---|---|---|---|
| 44.1 kHz | none | 32-1024 | Classic speed-1 vibrato is widened to p95 55-70 c (input +-40 c) at every buffer. CPU also fails at 32/64 (max 370 %/184 %). |
| 48 kHz | 256, 512, 1024 | 32, 64, 128 | CPU: heavy blocks of 2-2.5 ms. At 128 the max is 101 % (borderline; 93-104 % across runs). |
| 88.2 kHz | 512, 1024 | 32-256 | CPU: p99 83-334 %. At 256, max 118 %. |
| 96 kHz | 512, 1024 | 32-256 | CPU: p99 89-352 %. At 256, max 122 %. |

- Latency claim: reported 2885 samples at 44.1/48 k and 5770 at 88.2/96 k is honest for host PDC,
  and it stays constant.
- Do not claim sample-accurate timing:
  - Onsets and transients land after the reported delay: +6.5 to +7.8 ms on real vocals and +9 to
    +11 ms on synthetic transients at 44.1/48 k; +11 to +16 ms at 88.2/96 k.
  - At 48 k the offset of transients on silence depends on the buffer: +9.2 ms at 32, -0.8 ms at
    512/1024.
- Artifacts: clean in all 24 cells (note changes -41 to -55 dB, no clicks).
- Capacity: 2-4 instances per core at 44.1/48 k (512/1024), 1-2 at 88.2/96 k.

## Live Mode

No Live configuration is supported for a claim of clean or click-free tracking. The latency, CPU
and accuracy claims below hold, subject to the notes.

| Profile | Artifact verdict (all SR x buffer) | Matrix-supported cells | Matrix failures |
|---|---|---|---|
| Extreme 2MS | FAIL in 16/16: 11-12 of 12 note changes click up to +6 dB; a splice every pitch period on vibrato | 14/16 | 96 k/32 CPU (p99 80 %); 44.1 k/256 semitone settle 54 ms vs ref 28 |
| Ultra Quality C3+ | FAIL in 14/16: 1-5 of 12 note changes click (88.2 k/256 instead fails on a -11 dB tone step). The 2 passing cells (48 k/128, 44.1 k/256) still splice at >= -30 dB on 5 of 12 | 15/16 | 44.1 k/256 fifth settle 81 ms vs ref 53 |
| Extended Range | FAIL in 16/16: 2-7 of 12 note changes click (worse at 88.2/96 k) | 16/16 | none |

What may be claimed for Live, from this evidence:

- **Reported latency.**
  - Ultra is 11.90 ms at 44.1/48 k (525/571 samples) and 11.81/11.80 ms at 88.2/96 k.
  - Range is 21.6 ms at 44.1/48 k and 21.5 ms at 88.2/96 k.
  - For Ultra and Range the measured delay equals the report exactly, on clicks, on consonants and
    on real vocals (high-pass correlation 0.00 ms). Envelope correlation is mostly within +-1 ms,
    ranging from -3.3 to +3.1 ms.
- **Extreme "2 ms" is exact only for the dry/unvoiced path.**
  - Voiced, corrected audio lags about one pitch period more: +3.9 to +5.6 ms on a B3 tone and up
    to +11 ms on real vocals.
  - "2 ms latency" must not be presented as the delay of the tuned voice.
- **Real-time capacity (single instance, M4).**
  - Ultra and Range pass at every SR and buffer, 32 included.
  - Extreme passes everywhere except 96 k/32.
  - The old statement "Extreme is outside at 96 kHz / 64" is refuted: p99 40 %, max 51 %. It was a
    policy label in `auto_pitch_bench.cpp:727`, not a measurement.
  - Instances per core at p99: Extreme 2 (32), 5 (64), 9 (128) at 44.1/48 k; Ultra 6-91;
    Range 1-22 (the lower figures are at 88.2/96 k).
- **Accuracy.**
  - Ultra/Range settle within 0.2 c on B3, the low note (C3 / E2) and C5, at every cell.
  - Extreme: B3 error 3.6 c at 48 k (0.0 at 44.1/88.2 k, 1.0 at 96 k); C5 error 6.3 c at 48 k
    and 5.0 c at 44.1 k.
  - The vibrato gate passes everywhere.
- **Settling.**
  - Semitone/fifth settling grows with the buffer. Extreme: 28/58 ms at 48 k/128, 39/77 ms at 256.
  - Prefer 64-128 samples for tracking.

## Known issue list: confirm / refute

- **"96 kHz at <= 128 samples fails the CPU budget."**
  - Confirmed for Studio, and wider: 88.2/96 k fail up to 256 and 48 k fails at <= 128.
  - Refuted for Live: Ultra and Range pass at 96 k down to 32; Extreme fails only at 32.
- **"Extreme is outside at 96/64."** Refuted: it passes the CPU gate with margin.
- **New blocking items for any Live quality claim:**
  - Extreme clicks on every pitch movement (`SattariLivePitchEngine.h:725-730`, `:772-777`).
  - Ultra/Range splice clicks at note changes.
  - Extreme's voiced path lags its reported 2 ms (`SattariLivePitchEngine.h:824-825`).
- **New blocking item for Studio at 44.1 kHz:** vibrato widening under Classic / speed 1.

## Re-qualification

Run `AutoPitchLiveMatrix` (see the Reproduce section of the dated report) on the candidate build.
Then replace this file from the new `--merge` output. A cell becomes supported only when both
verdicts pass.
