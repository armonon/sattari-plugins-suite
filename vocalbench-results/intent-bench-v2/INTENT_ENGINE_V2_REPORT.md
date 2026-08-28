# Intent Engine v2 Report

**Baseline commit:** `22a594d` | **Production commit:** `6c022cb`
**Corpus:** 90 cases analyzed

---

## Summary
```
INTENT ENGINE V2 REPORT
Voiced frames: 13,262

Stable-target accuracy:      prod=47.9% v2=81.3%
Destination accuracy:        prod=3.7% v2=48.3%
Passing-tone preservation:   prod=93.3% v2=94.4%
False-target rate:           prod=52.2% v2=18.8%
Chatter rate:                prod=35.2% v2=10.3%
Octave errors:               prod=0 v2=0
```

---

## Phase 6 — GO/HOLD Gate

```
GO — ACTIVE AUDIO EXPERIMENT
```

---

## Phase 5 — Metric Dimensions by Category

| Category | N | Prod Stab | v2 Stab | Prod Dest | v2 Dest | v2 Pass Pres | v2 FT | v2 Chat |
|----------|---|-----------|---------|-----------|---------|--------------|-------|---------|
| adv_boundary | 10 | 0% | 97% | — | — | — | 3% | 1% |
| adv_chromatic_pass | 7 | 60% | 61% | — | — | 100% | 39% | 7% |
| adv_slide | 24 | 61% | 61% | 0% | 39% | — | 33% | 5% |
| adv_vib_boundary | 8 | — | — | — | — | — | — | 29% |
| boundary | 6 | 0% | 98% | — | — | — | 2% | 1% |
| chromatic_passing | 4 | 52% | 57% | — | — | 100% | 43% | 5% |
| fall | 2 | 94% | 94% | 0% | 66% | — | 4% | 2% |
| out_of_scale | 2 | 92% | 94% | — | — | — | 6% | 4% |
| repeated_note | 2 | 48% | 76% | — | — | — | 24% | 17% |
| run | 4 | 69% | 55% | — | — | 43% | 45% | 23% |
| scoop | 3 | 95% | 97% | 0% | 82% | — | 2% | 2% |
| semitone_slide | 5 | 90% | 94% | 39% | 31% | — | 6% | 3% |
| short_note | 6 | 52% | 63% | — | — | — | 37% | 12% |
| vibrato_boundary | 3 | — | — | — | — | — | — | 36% |
| whole_tone_slide | 2 | 94% | 93% | 0% | 25% | — | 6% | 5% |
| wrong_note | 2 | 92% | 92% | — | — | — | 8% | 7% |

---

## Phase 3 — Shadow v2 Reason Code Distribution

| Reason | Frames | % |
|--------|--------|---|
| NEAREST_STABLE | 9,614 | 72.5% |
| SLIDE_DESTINATION | 3,648 | 27.5% |

---

## Worst Failure Clusters

| Case | Category | Prod Stab | v2 Stab | v2 Pass Pres |
|------|----------|-----------|---------|--------------|
| RUN_up_80ms | run | 57% | 14% | 89% |
| SHORT_A3_80ms | short_note | 14% | 29% | — |
| SHORT_A3_100ms | short_note | 22% | 44% | — |
| RUN_up_120ms | run | 60% | 50% | 40% |
| CHROM_pass_Bb3_60ms | chromatic_passing | 53% | 54% | 100% |
| CHROM_pass_Bb3_100ms | chromatic_passing | 52% | 58% | 100% |
| CHROM_pass_Bb3_150ms | chromatic_passing | 52% | 58% | 100% |
| CHROM_pass_Bb3_200ms | chromatic_passing | 53% | 59% | 100% |

---

## v2 Architecture — Phase 3

Input features: detectedMidi, smoothedVelocity (12-frame history),
directionConsistency, dwellFrames per candidate, prevStableTarget,
stabilityDuration, confidence, key/scale.

Decision tiers:
1. **SLIDE_DESTINATION**: abs(smoothedVel) ≥ 0.6 MIDI/sec + consistent →
   extrapolate 200-350ms ahead, snap to nearest scale note.
2. **PASSING_TONE**: consistent direction + dwell < 180ms + stable target known →
   hold previous stable target, do not commit to passing note.
3. **STABILITY_ANCHOR**: stable ≥ 100ms + conf ≥ 0.55 + low velocity →
   lock to nearest scale note with high confidence.
4. **VIBRATO_HOLD**: stable target known + low velocity → preserve it.
5. **LOW_CONFIDENCE_FALLBACK**: fall through to production target.

Strategy G correction dynamics: **FROZEN**.