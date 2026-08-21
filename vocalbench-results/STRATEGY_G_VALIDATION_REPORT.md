# Auto Pitch — Strategy G Production Validation Report

**Branch:** `experiment/performance-adaptive-strategy-g`
**Total bench runs:** 105 (35 cases × 3 strategies)
**Strategies compared:** A (baseline), D (full+slow), G (state-adaptive)

---

## 1. Full Regression vs Benchmark Suite

All 1,344 original benchmark runs re-executed. Results from `experiment_aggregated.json`:

| Metric | A | D | G | G vs D |
|--------|---|---|---|--------|
| SS error (stable) | 11.37 | 0.00 | 0.00 | = 0.000 |
| Mean abs error (stable) | 11.46 | 0.53 | 0.46 | ↓ 0.062 |
| p95 abs error (stable) | 11.45 | 2.65 | 2.16 | ↓ 0.493 |
| Efficiency | 0.48 | 0.92 | 0.92 | = 0.000 |
| Vibrato depth retained | 19.57 | 22.44 | 22.44 | = 0.008 |
| Vibrato center error | 1.60 | -0.34 | 0.01 | ↑ 0.346 |
| Vibrato jitter | 13.70 | 15.82 | 15.70 | ↓ 0.122 |
| Post-slide error | 16.01 | 0.98 | 0.54 | ↓ 0.440 |
| Boundary SS error | 23.88 | 0.00 | 0.00 | = 0.000 |
| False targets (boundary) | 0 | 0 | 0 | = 0.000 |
| Octave errors (boundary) | 0 | 0 | 0 | = 0.000 |
| Duration mean error | 14.75 | 2.56 | 2.06 | ↓ 0.490 |
| Duration SS error | 14.31 | 0.61 | 0.61 | = 0.005 |

**Crashes across all 105 validation runs:** 0
**False targets:** 0 (verified from benchmark regression)
**Octave errors:** 0 (verified from benchmark regression)

---

## 2. State-Transition Results

Metric: maximum frame-to-frame output pitch jump (voiced frames only), in cents.
A jump > 2c between adjacent blocks may be audible as a chirp or snap.

### Per-case worst discontinuity

| Test | Description | A max jump | D max jump | G max jump | G p95 jump |
|------|-------------|-----------|-----------|-----------|-----------|
| T01_attack_sustain | Attack → Sustain: 50ms silence → 3s held A3 –30c | 3.41c | 5.06c | 5.02c ⚠️ | 0.14c |
| T02_attack_sustain_vibrato | Attack → 1s sustain → 2s vibrato 5Hz/±30c, A3 –25c | 2.89c | 4.19c | 4.09c | 1.43c |
| T03_vibrato_release | 2s vibrato → fade to release → silence | 2.60c | 2.20c | 4.34c | 1.66c |
| T04_slide_sustain | A3 sustain → 250ms slide → B3 sustain, both –30c o | 35.77c | 30.47c | 34.87c ⚠️ | 2.90c |
| T05_attack_slide | 50ms silence → 200ms A3 → 200ms slide → B3 sustain | 34.27c | 30.75c | 35.33c ⚠️ | 2.81c |
| T06_rapid_notes | 10 × 100ms notes (80ms on, 20ms off), A3 –25c | 3.46c | 4.50c | 5.57c ⚠️ | 1.35c |
| T07_legato_change | A3 → B3 → A3 legato, –30c offset each | 20.60c | 23.68c | 27.18c ⚠️ | 1.18c |
| T08_slow_slide | A3 → 500ms slow slide → B3, –20c offset | 40.44c | 38.65c | 44.23c ⚠️ | 2.61c |
| T09_fast_slide | A3 → 75ms fast slide → B3, –20c offset | 25.25c | 24.63c | 28.23c ⚠️ | 1.30c |
| T10_vibrato_boundary_cross | 3s vibrato ±70c around A3+50c — crosses A3↔B3 boun | 37.87c | 43.09c | 42.15c ⚠️ | 32.61c |
| T11_vibrato_onset | 1.5s flat → vibrato appears mid-note, A3 –20c | 2.36c | 3.29c | 3.86c | 1.18c |
| T12_vibrato_offset | 1.5s vibrato → goes flat mid-note, A3 –20c | 2.60c | 2.20c | 4.34c | 1.42c |
| T13_vibrato_rate_change | Vibrato rate changes 3.5Hz → 7Hz mid-note | 2.10c | 2.38c | 2.69c | 1.14c |
| T14_vibrato_depth_change | Vibrato depth changes ±10c → ±40c mid-note | 3.79c | 2.66c | 3.04c | 1.87c |
| T15_very_short_notes | 8 × 50ms notes, A3 –25c | 3.18c | 4.23c | 5.17c ⚠️ | 3.73c |
| T16_250ms_notes | 4 × 250ms notes, A3 –30c, 50ms gaps | 4.37c | 5.04c | 6.68c ⚠️ | 2.89c |
| T17_breathy_attack | Soft attack first 300ms (amplitude 0.03), then nor | 3.45c | 5.04c | 6.04c ⚠️ | 0.12c |
| T18_f0_dropout | 500ms note → 200ms dropout → resume, A3 –25c | 9.55c | 12.80c | 21.67c ⚠️ | 1.30c |
| T19_register_change | A3 → A4 with 30ms gap, –20c offset each | 2.28c | 3.29c | 3.86c | 0.15c |
| T20_abrupt_interruption | 2s sustain → abrupt 5ms cutoff → resume | 12.21c | 15.97c | 27.22c ⚠️ | 1.21c |
| T21_adversarial_sequence | attack → vibrato → slide → sustain (full adversari | 31.29c | 27.89c | 31.96c ⚠️ | 2.94c |
| T22_slide_then_vibrato | A3 → 150ms slide → vibrato B3, no sustain between | 31.42c | 27.15c | 29.99c ⚠️ | 3.30c |
| T23_note_change_in_vibrato | Vibrato on A3 → note changes to B3 while still vib | 11.50c | 15.08c | 17.23c ⚠️ | 2.94c |
| T24_slow_wobble | Slow oscillation 1.5Hz ±15c — below vibrato rate,  | 2.06c | 3.00c | 3.48c | 0.70c |
| T25_unstable_attack | 150ms F0 scoop (–60c → –20c) before settling on A3 | 6.49c | 9.74c | 11.83c ⚠️ | 1.14c |
| T26_sustain_vibrato_alternate | 4 × (500ms flat → 500ms vibrato) — stress sustain↔ | 2.86c | 4.17c | 4.96c | 1.20c |
| T27_low_register | A2 –30c, 3s held — low register pitch detection st | 3.31c | 4.89c | 6.19c ⚠️ | 0.12c |
| T28_high_register_vibrato | A4 –20c, 3s vibrato 6Hz/±20c — high register | 2.24c | 2.00c | 4.10c | 1.07c |
| T29_aggressive_attack | Harsh 20ms ramp attack → 2.5s sustain, A3 –30c | 9.78c | 10.38c | 14.20c ⚠️ | 0.12c |
| T30_jittery_repeated_notes | 10 × 100ms notes with varying pitch offset (–20 to | 2.28c | 3.29c | 3.86c | 1.22c |
| T31_octave_risk_vibrato | A3 vibrato ±550c — briefly enters A4 range (octave | 32.60c | 34.65c | 56.11c ⚠️ | 38.88c |
| T32_large_offset_long | A3 –49c, 4s held — max offset, long hold | 5.65c | 8.38c | 10.22c ⚠️ | 0.08c |
| T33_small_offset_vibrato_center | A3 –5c center with ±30c vibrato — small offset, de | 3.06c | 2.50c | 4.50c | 1.84c |
| T34_positive_offset | A3 +35c (sharp), 3s held | 4.11c | 6.28c | 7.52c ⚠️ | 0.11c |
| T35_slide_sustained_vibrato | A3 → slide → B3 sustained vibrato with –25c center | 34.02c | 29.80c | 33.08c ⚠️ | 3.10c |

### Five worst G cases by max jump

| Rank | Test | Max jump | p95 jump | Chat rate | SV flips |
|------|------|---------|---------|----------|---------|
| 1 | T31_octave_risk_vibrato | 56.11c | 38.88c | 0.0106 | 0 |
| 2 | T08_slow_slide | 44.23c | 2.61c | 0.0213 | 1 |
| 3 | T10_vibrato_boundary_cross | 42.15c | 32.61c | 0.0957 | 2 |
| 4 | T05_attack_slide | 35.33c | 2.81c | 0.0267 | 1 |
| 5 | T04_slide_sustain | 34.87c | 2.90c | 0.0197 | 1 |

---

## 3. State Chatter Analysis

State chatter: rapid oscillation between states without settling.
Measured as flips/voiced-frame. > 0.10 = potential audible instability.

| Test | Description | Chat rate | SV flips | Dominant state |
|------|-------------|----------|---------|---------------|
| T01_attack_sustain | Attack → Sustain: 50ms silence → 3s held A3 –30c | 0.0142 | 1 | sustain |
| T02_attack_sustain_vibrato | Attack → 1s sustain → 2s vibrato 5Hz/±30c, A3 –25c | 0.0142 | 1 | sustain |
| T03_vibrato_release | 2s vibrato → fade to release → silence | 0.0211 | 1 | sustain |
| T04_slide_sustain | A3 sustain → 250ms slide → B3 sustain, both –30c o | 0.0197 | 1 | sustain |
| T05_attack_slide | 50ms silence → 200ms A3 → 200ms slide → B3 sustain | 0.0267 | 1 | sustain |
| T06_rapid_notes | 10 × 100ms notes (80ms on, 20ms off), A3 –25c | 0.0426 | 1 | sustain |
| T07_legato_change | A3 → B3 → A3 legato, –30c offset each | 0.0284 | 1 | sustain |
| T08_slow_slide | A3 → 500ms slow slide → B3, –20c offset | 0.0213 | 1 | sustain |
| T09_fast_slide | A3 → 75ms fast slide → B3, –20c offset | 0.0208 | 1 | sustain |
| T10_vibrato_boundary_cross | 3s vibrato ±70c around A3+50c — crosses A3↔B3 boun | 0.0957 | 2 | sustain |
| T11_vibrato_onset | 1.5s flat → vibrato appears mid-note, A3 –20c | 0.0142 | 1 | sustain |
| T12_vibrato_offset | 1.5s vibrato → goes flat mid-note, A3 –20c | 0.0142 | 1 | sustain |
| T13_vibrato_rate_change | Vibrato rate changes 3.5Hz → 7Hz mid-note | 0.0142 | 1 | sustain |
| T14_vibrato_depth_change | Vibrato depth changes ±10c → ±40c mid-note | 0.0142 | 1 | sustain |
| T15_very_short_notes | 8 × 50ms notes, A3 –25c | 0.0789 | 0 | vibrato |
| T16_250ms_notes | 4 × 250ms notes, A3 –30c, 50ms gaps | 0.1264 ⚠️ | 0 | trans |
| T17_breathy_attack | Soft attack first 300ms (amplitude 0.03), then nor | 0.0142 | 1 | sustain |
| T18_f0_dropout | 500ms note → 200ms dropout → resume, A3 –25c | 0.0336 | 2 | sustain |
| T19_register_change | A3 → A4 with 30ms gap, –20c offset each | 0.0246 | 2 | sustain |
| T20_abrupt_interruption | 2s sustain → abrupt 5ms cutoff → resume | 0.0282 | 2 | sustain |
| T21_adversarial_sequence | attack → vibrato → slide → sustain (full adversari | 0.0241 | 1 | sustain |
| T22_slide_then_vibrato | A3 → 150ms slide → vibrato B3, no sustain between | 0.0241 | 1 | sustain |
| T23_note_change_in_vibrato | Vibrato on A3 → note changes to B3 while still vib | 0.0255 | 1 | sustain |
| T24_slow_wobble | Slow oscillation 1.5Hz ±15c — below vibrato rate,  | 0.0142 | 1 | sustain |
| T25_unstable_attack | 150ms F0 scoop (–60c → –20c) before settling on A3 | 0.0161 | 1 | sustain |
| T26_sustain_vibrato_alternate | 4 × (500ms flat → 500ms vibrato) — stress sustain↔ | 0.0107 | 1 | sustain |
| T27_low_register | A2 –30c, 3s held — low register pitch detection st | 0.0142 | 1 | sustain |
| T28_high_register_vibrato | A4 –20c, 3s vibrato 6Hz/±20c — high register | 0.0142 | 1 | sustain |
| T29_aggressive_attack | Harsh 20ms ramp attack → 2.5s sustain, A3 –30c | 0.0169 | 1 | sustain |
| T30_jittery_repeated_notes | 10 × 100ms notes with varying pitch offset (–20 to | 0.0426 | 1 | sustain |
| T31_octave_risk_vibrato | A3 vibrato ±550c — briefly enters A4 range (octave | 0.0106 | 0 | attack |
| T32_large_offset_long | A3 –49c, 4s held — max offset, long hold | 0.0107 | 1 | sustain |
| T33_small_offset_vibrato_center | A3 –5c center with ±30c vibrato — small offset, de | 0.0142 | 1 | sustain |
| T34_positive_offset | A3 +35c (sharp), 3s held | 0.0142 | 1 | sustain |
| T35_slide_sustained_vibrato | A3 → slide → B3 sustained vibrato with –25c center | 0.0236 | 1 | sustain |

**High-chatter cases (rate > 0.10):** 1
- `T16_250ms_notes` 4 × 250ms notes, A3 –30c, 50ms gaps: rate=0.1264

---

## 4. Adversarial Sequence Results

| Test | Description | Max jump | Chat rate | SS error | Verdict |
|------|-------------|---------|----------|---------|---------|
| T21_adversarial_sequence | attack → vibrato → slide → sustain (full adversari | 31.96c | 0.0241 | 0.03c | FLAG |
| T22_slide_then_vibrato | A3 → 150ms slide → vibrato B3, no sustain between | 29.99c | 0.0241 | 14.85c | FLAG |
| T23_note_change_in_vibrato | Vibrato on A3 → note changes to B3 while still vib | 17.23c | 0.0255 | 14.42c | FLAG |
| T24_slow_wobble | Slow oscillation 1.5Hz ±15c — below vibrato rate,  | 3.48c | 0.0142 | 7.58c | PASS |
| T25_unstable_attack | 150ms F0 scoop (–60c → –20c) before settling on A3 | 11.83c | 0.0161 | 0.01c | FLAG |
| T26_sustain_vibrato_alternate | 4 × (500ms flat → 500ms vibrato) — stress sustain↔ | 4.96c | 0.0107 | 8.28c | PASS |
| T30_jittery_repeated_notes | 10 × 100ms notes with varying pitch offset (–20 to | 3.86c | 0.0426 | 3.19c | PASS |
| T31_octave_risk_vibrato | A3 vibrato ±550c — briefly enters A4 range (octave | 56.11c | 0.0106 | 27.09c | FLAG |

**Flagged adversarial cases:** 5
- `T21_adversarial_sequence` attack → vibrato → slide → sustain (full adversari: max jump 31.96c, chat rate 0.0241
- `T22_slide_then_vibrato` A3 → 150ms slide → vibrato B3, no sustain between: max jump 29.99c, chat rate 0.0241
- `T23_note_change_in_vibrato` Vibrato on A3 → note changes to B3 while still vib: max jump 17.23c, chat rate 0.0255
- `T25_unstable_attack` 150ms F0 scoop (–60c → –20c) before settling on A3: max jump 11.83c, chat rate 0.0161
- `T31_octave_risk_vibrato` A3 vibrato ±550c — briefly enters A4 range (octave: max jump 56.11c, chat rate 0.0106

---

## 5. Real Vocal Corpus

**NOT MEASURED — no real-vocal corpus exists in this repository.**

The only audio files available are synthetically generated (4-harmonic additive synthesis).
Blind perceptual comparison of A vs D vs G on real vocals was not possible.

Implication: all discontinuity and accuracy claims are based on synthetic stimuli.
Real-world vocal behavior (breathiness, intonation variability, subharmonics,
register breaks, throat transitions) is not covered. This is an open validation gap.

---

## 6. Five Worst G Cases (Overall)

Ranked by max frame-to-frame output pitch jump:

| Rank | Test | Description | Max jump | p95 jump | Chat rate | SV flips | SS error |
|------|------|-------------|---------|---------|----------|---------|---------|
| 1 | T31_octave_risk_vibrato | A3 vibrato ±550c — briefly enters A4 ran | 56.11c | 38.88c | 0.0106 | 0 | 27.09c |
| 2 | T08_slow_slide | A3 → 500ms slow slide → B3, –20c offset | 44.23c | 2.61c | 0.0213 | 1 | 0.20c |
| 3 | T10_vibrato_boundary_cross | 3s vibrato ±70c around A3+50c — crosses  | 42.15c | 32.61c | 0.0957 | 2 | 38.77c |
| 4 | T05_attack_slide | 50ms silence → 200ms A3 → 200ms slide →  | 35.33c | 2.81c | 0.0267 | 1 | 0.01c |
| 5 | T04_slide_sustain | A3 sustain → 250ms slide → B3 sustain, b | 34.87c | 2.90c | 0.0197 | 1 | 0.01c |

---

## 7. Regressions

**No regressions vs D on benchmark suite metrics.**
G meets or exceeds D on all measured dimensions in the standard corpus.

**Discontinuity flags (max jump > 5c in torture suite):**
- `T31_octave_risk_vibrato` A3 vibrato ±550c — briefly enters A4 range (octave: 56.11c jump
- `T08_slow_slide` A3 → 500ms slow slide → B3, –20c offset: 44.23c jump
- `T10_vibrato_boundary_cross` 3s vibrato ±70c around A3+50c — crosses A3↔B3 boun: 42.15c jump
- `T05_attack_slide` 50ms silence → 200ms A3 → 200ms slide → B3 sustain: 35.33c jump
- `T04_slide_sustain` A3 sustain → 250ms slide → B3 sustain, both –30c o: 34.87c jump
- `T35_slide_sustained_vibrato` A3 → slide → B3 sustained vibrato with –25c center: 33.08c jump
- `T21_adversarial_sequence` attack → vibrato → slide → sustain (full adversari: 31.96c jump
- `T22_slide_then_vibrato` A3 → 150ms slide → vibrato B3, no sustain between: 29.99c jump
- `T09_fast_slide` A3 → 75ms fast slide → B3, –20c offset: 28.23c jump
- `T20_abrupt_interruption` 2s sustain → abrupt 5ms cutoff → resume: 27.22c jump
- `T07_legato_change` A3 → B3 → A3 legato, –30c offset each: 27.18c jump
- `T18_f0_dropout` 500ms note → 200ms dropout → resume, A3 –25c: 21.67c jump
- `T23_note_change_in_vibrato` Vibrato on A3 → note changes to B3 while still vib: 17.23c jump
- `T29_aggressive_attack` Harsh 20ms ramp attack → 2.5s sustain, A3 –30c: 14.20c jump
- `T25_unstable_attack` 150ms F0 scoop (–60c → –20c) before settling on A3: 11.83c jump
- `T32_large_offset_long` A3 –49c, 4s held — max offset, long hold: 10.22c jump
- `T34_positive_offset` A3 +35c (sharp), 3s held: 7.52c jump
- `T16_250ms_notes` 4 × 250ms notes, A3 –30c, 50ms gaps: 6.68c jump
- `T27_low_register` A2 –30c, 3s held — low register pitch detection st: 6.19c jump
- `T17_breathy_attack` Soft attack first 300ms (amplitude 0.03), then nor: 6.04c jump
- `T06_rapid_notes` 10 × 100ms notes (80ms on, 20ms off), A3 –25c: 5.57c jump
- `T15_very_short_notes` 8 × 50ms notes, A3 –25c: 5.17c jump
- `T01_attack_sustain` Attack → Sustain: 50ms silence → 3s held A3 –30c: 5.02c jump

---

## Verdict

**G BLOCKED — TRANSITION FAILURE**

23 torture case(s) show max jump > 5c — potential audible artifact.

### Exact recommendation

Return to architecture work. Identify root cause of failure before proceeding.

---

*All results from `experiment/performance-adaptive-strategy-g` branch.*
*No production code changed. All bench runs exited 0 (no crashes, NaN, or Inf).*