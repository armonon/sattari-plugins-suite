# Intent Engine v2 — ACTIVE AUDIO REPORT

**Phase:** ACTIVE AUDIO EXPERIMENT (C++ v2_active mode drives real audio path)
**Corpus:** 90 cases | **Voiced frames:** 13,262 | **v2 driving:** 13,119 (98.9%)

---

## Verdict

```
HOLD — PARITY (no regression, but improvement not confirmed)
```

---

## Aggregate Metrics

| Metric | Production | v2 Active | Delta |
|--------|-----------|-----------|-------|
| Stable-target accuracy | 86.4% | 86.4% | +0.0% |
| Destination accuracy | 55.2% | 55.2% | +0.0% |
| Passing-tone preservation | 92.3% | 92.3% | +0.0% |
| False-target rate (lower=better) | 13.7% | 13.7% | +0.0% |
| Chatter rate (lower=better) | 1.8% | 1.8% | -0.0% |
| Octave errors | 0 | 0 | — |

---

## v2 Driving Breakdown

- **v2 driving rate:** 98.9% (13,119 / 13,262 voiced frames)
- **Fallback rate:** 1.1% (fell back to production target)

### C++ Reason Code Distribution (all voiced frames)

| Reason | Frames | % |
|--------|--------|---|
| STABILITY_ANCHOR | 6,674 | 50.3% |
| NEAREST_STABLE | 3,991 | 30.1% |
| VIBRATO_HOLD | 1,445 | 10.9% |
| PASSING_TONE | 1,151 | 8.7% |
| LOW_CONFIDENCE_FALLBACK | 1 | 0.0% |

---

## By Category

| Category | N | Prod Stab | Active Stab | Prod Dest | Active Dest | Active Pass | Active FT | Drive% |
|----------|---|-----------|-------------|-----------|-------------|-------------|-----------|--------|
| adv_boundary | 10 | 100.0% | 100.0% | — | — | — | 0.0% | 99% |
| adv_chromatic_pass | 7 | 66.0% | 66.0% | — | — | 100.0% | 34.0% | 99% |
| adv_slide | 24 | 66.3% | 66.3% | 46.4% | 46.4% | — | 33.7% | 99% |
| adv_vib_boundary | 8 | — | — | — | — | 100.0% | — | 99% |
| boundary | 6 | 100.0% | 100.0% | — | — | — | 0.0% | 99% |
| chromatic_passing | 4 | 63.6% | 63.6% | — | — | 100.0% | 36.4% | 99% |
| fall | 2 | 100.0% | 100.0% | 100.0% | 100.0% | — | 0.0% | 99% |
| other | 2 | 100.0% | 100.0% | — | — | 100.0% | 0.0% | 99% |
| repeated_note | 2 | 100.0% | 100.0% | — | — | — | 0.0% | 94% |
| run | 4 | 93.8% | 93.8% | — | — | 3.5% | 6.2% | 92% |
| scoop | 3 | 100.0% | 100.0% | 100.0% | 100.0% | — | 0.0% | 99% |
| semitone_slide | 5 | 99.3% | 99.3% | 84.4% | 84.4% | — | 0.7% | 99% |
| short_note | 6 | 100.0% | 100.0% | — | — | — | 0.0% | 95% |
| vibrato_boundary | 3 | — | — | — | — | 100.0% | — | 99% |
| whole_tone_slide | 2 | 100.0% | 100.0% | 15.7% | 15.7% | — | 0.0% | 99% |
| wrong_note | 2 | 98.2% | 98.2% | — | — | — | 9.0% | 95% |

---

## Worst Cases (by Stable-Target Delta)

| Case | Category | Prod Stab | Active Stab | Delta |
|------|----------|-----------|-------------|-------|
| ADV_BC_+45c | adv_boundary | 100.0% | 100.0% | +0.0% |
| ADV_BC_+48c | adv_boundary | 100.0% | 100.0% | +0.0% |
| ADV_BC_+52c | adv_boundary | 100.0% | 100.0% | +0.0% |
| ADV_BC_+55c | adv_boundary | 100.0% | 100.0% | +0.0% |
| ADV_BC_+58c | adv_boundary | 100.0% | 100.0% | +0.0% |
| ADV_BC_-45c | adv_boundary | 100.0% | 100.0% | +0.0% |
| ADV_BC_-48c | adv_boundary | 100.0% | 100.0% | +0.0% |
| ADV_BC_-52c | adv_boundary | 100.0% | 100.0% | +0.0% |
| ADV_BC_-55c | adv_boundary | 100.0% | 100.0% | +0.0% |
| ADV_BC_-58c | adv_boundary | 100.0% | 100.0% | +0.0% |

---

## Architecture Note

In V2_ACTIVE mode, the C++ intent engine drives `targetMidi` when `intentConf ≥ 0.68`
(high-confidence) or when `intentConf ≥ 0.50` with STABILITY_ANCHOR / VIBRATO_HOLD /
PASSING_TONE reason. Below that threshold, the engine falls back to the production target.
Strategy G correction dynamics (transitionBoost, hysteresis) remain frozen.

**Active target = intentTarget when !intentFallback, else prodTarget.**

---

## Root Cause Analysis — Why Parity, Not Improvement

v2 active drives 98.9% of frames but produces **identical metric values** to production.
The reason: the three surviving tiers all reproduce production's target-selection logic.

| Tier | % of frames | What it does | vs. Production |
|------|-------------|--------------|----------------|
| STABILITY_ANCHOR | 50.3% | Locks onto the last stable scale note | Same as prod (stability hysteresis) |
| NEAREST_STABLE | 30.1% | Maps detected pitch to nearest scale note | Same as prod (scale quantization) |
| VIBRATO_HOLD | 10.9% | Holds anchor through vibrato oscillation | Same as prod (low-conf hold) |
| PASSING_TONE | 8.7% | Suppresses commitment during transitions | Holds `stableTarget` = prod's anchor |

PASSING_TONE is the only tier that can diverge from production — but `stableTarget` is set from
the same stability logic that production uses, so the held value matches what production would
choose anyway.

### SLIDE_DESTINATION Was the Only Divergent Tier

SLIDE_DESTINATION was the one tier that extrapolated *beyond* the detected pitch to a predicted
destination. Disabling it (required due to YIN quantization artifacts — see below) eliminated
every case where v2 could have diverged meaningfully from production.

---

## Bugs Fixed During This Experiment

### Bug 1 — Onset Velocity Artifact (SLIDE_DESTINATION)
- **Symptom:** SLIDE_DESTINATION fired on note onsets (first 1-2 blocks), extrapolating the
  YIN-quantized pitch step (20-30 MIDI/sec) as a genuine slide velocity
- **Fix 1 attempted:** `histFill >= 4` guard — insufficient (pitchHopSize=256 → 2 hops/block,
  so histFill=4 reached by block 1)
- **Fix 2 attempted:** `histFill >= 8` (4 blocks, ~46ms) — reduced onset cases but core
  velocity-overshoot problem persisted
- **Root cause:** YIN produces discrete integer-MIDI-aligned values; any pitch transition yields
  velocity ≈ 20-30 MIDI/sec regardless of gesture speed. `svel * 0.22` extrapolation overshoots
  by 6+ MIDI on ordinary ornaments (falls, scoops)
- **Final fix:** Disabled SLIDE_DESTINATION entirely (`if (false)`) with detailed comment
  documenting the YIN quantization constraint

### Bug 2 — Stale-Anchor PASSING_TONE Hold During Scale Runs
- **Symptom:** PASSING_TONE held `stableTarget=A3(57)` through an ascending scale run
  (A3→B3→C4→D4→E4), suppressing all intermediate notes but locking the anchor at A3 instead
  of following the run's destination
- **Fix:** Added directional escape guard: `dir * (det - s.stableTarget) <= 0.5` so PASSING_TONE
  only fires when the pitch is NOT consistently departing from the anchor
- **Result:** Run accuracy improved from ~82% to ~94% (on-par with production)

---

## Next Steps for v2 to Achieve Genuine GO

To get measurable improvement over production, v2 needs a tier that can correctly diverge:

1. **Better velocity source for SLIDE_DESTINATION:** Sub-sample pitch interpolation or a
   longer smoothed derivative would yield accurate slide velocity. Re-enable SLIDE_DESTINATION
   only with a velocity source whose error is < 2 MIDI/sec at typical slide speeds (3-8 MIDI/sec).

2. **Cross-note stability extension:** A tier that delays commitment to a new note slightly
   longer than production's hysteresis — useful for rapid repeated notes or ornamental figures.
   This is distinct from PASSING_TONE (which suppresses, not delays).

3. **Destination inference from phrase context:** The architecture is in place (histFill, dwell,
   stableTarget). A phrase-aware destination predictor (e.g. scale-direction extrapolation for
   runs, neighbor-note return probability for ornaments) could give meaningful dest-acc lift.