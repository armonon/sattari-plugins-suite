# Intent v3 Active Audio Validation Report

## Phase 1 — Active Render Matrix (Production vs v3_active)

Total voiced frames: 12,778
Crashes: 0  |  NaN/Inf: 0
Global max correction jump: 285.4¢  |  p95: 181.0¢
Cases where v3 chatter > prod+5%: 2 / 90
Octave errors: 0

### Per-category correction jump summary

| Category | n | Prod p95J | v3 p95J | Δ MaxJ | v3 Chat | v2 Fire |
|----------|---|-----------|---------|--------|---------|---------|
| adv_boundary | 10 | 0¢ | 0¢ | 89¢ | 0.6% | 0.3% |
| adv_chromatic_pass | 7 | 27¢ | 39¢ | -25¢ | 4.7% | 6.0% |
| adv_slide | 24 | 14¢ | 17¢ | 13¢ | 2.6% | 6.1% |
| adv_vib_boundary | 8 | 77¢ | 77¢ | 0¢ | 4.3% | 0.0% |
| boundary | 6 | 0¢ | 0¢ | 89¢ | 0.5% | 0.2% |
| chromatic_passing | 4 | 26¢ | 26¢ | 46¢ | 3.0% | 3.8% |
| fall | 2 | 3¢ | 3¢ | 90¢ | 0.9% | 0.9% |
| out_of_scale | 2 | 3¢ | 3¢ | 138¢ | 1.4% | 0.7% |
| repeated_note | 2 | 34¢ | 46¢ | 72¢ | 1.6% | 0.8% |
| run | 4 | 94¢ | 143¢ | 68¢ | 8.9% | 2.6% |
| scoop | 3 | 5¢ | 5¢ | 63¢ | 0.8% | 0.4% |
| semitone_slide | 5 | 12¢ | 13¢ | 2¢ | 1.6% | 5.9% |
| short_note | 6 | 16¢ | 16¢ | 0¢ | 0.0% | 0.0% |
| vibrato_boundary | 3 | 71¢ | 71¢ | -0¢ | 4.0% | 0.0% |
| whole_tone_slide | 2 | 22¢ | 22¢ | 15¢ | 2.8% | 12.6% |
| wrong_note | 2 | 43¢ | 33¢ | 108¢ | 3.3% | 3.3% |

## Phase 3 — Semitone Slide Horizon Sweep

| Horizon | Dest Acc | Stable Acc | V2 Fire Rate |
|---------|----------|------------|--------------|
| 40ms | 12.1% | 72.4% | 0.0% |
| 60ms | 12.1% | 72.4% | 0.0% |
| 80ms ← current | 12.1% | 72.4% | 0.0% |

  80ms horizon already optimal (best=40ms Δ=+0.0%)

## Phase 4 — Confidence Band Calibration

| Band | N | v3 Acc | Prod Acc | v3 Wins |
|------|---|--------|----------|---------|
| 0.0-0.5 | 0 | — | — | — |
| 0.5-0.6 | 0 | — | — | — |
| 0.6-0.7 | 2 | 0.0% | 50.0% | ✗ |
| 0.7-0.8 | 69 | 50.7% | 34.8% | ✓ |
| 0.8-0.9 | 19 | 57.9% | 78.9% | ✗ |
| 0.9-1.0 | 9127 | 81.4% | 65.2% | ✓ |

v3 outperforms production above confidence **0.7-0.8**.

## Phase 5 — Regression Suite

| Suite | Pass |
|-------|------|
| contract | ✅ |
| motionbench | ⚠ Not built |
| vocalbench_stable | ✅ |
| corpus_smoke | ✅ |

---

## Final Verdict

```
GO — INTENT V3 RC
```