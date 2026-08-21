# Auto Pitch — Strategy G Final Release Report

**Decision: MERGED — STRATEGY G**

---

## Merge Summary

| Item | Value |
|------|-------|
| Production commit (main) | `6c022cb` |
| DSP freeze commit (experiment) | `c971e13` |
| Branch merged from | `experiment/performance-adaptive-strategy-g` |
| Rollback commit | `ac4eb17` (revert of failed cherry-pick) — rollback: `git revert 6c022cb` |
| Merge date | 2026-08-21 |

---

## What Was Merged

Two production files changed:

| File | Change |
|------|--------|
| `plugins/auto-pitch/src/PluginProcessor.h` | +2 private members: `hysteresisAltTarget`, `hysteresisAge` |
| `plugins/auto-pitch/src/PluginProcessor.cpp` | Reset in `resetPitchState()` + transitionBoost block with hysteresis clamping |

**No UI changes. No parameter changes. No behavior change for non-vibrato-oscillation content.**

The change: when the same two correction targets alternate within 150 ms (vibrato crossing a scale-note boundary), `transitionBoost` is clamped to ≤ 0.30 instead of spiking to 1.0. Genuine note changes still fire the full boost.

---

## Gate Results

### Hard Gates

| Gate | Result | Status |
|------|--------|--------|
| T10 vibrato boundary cross ≤ 45c | 42.15c | ✅ (was 69.26c pre-fix) |
| False targets | 0 | ✅ |
| Octave errors | 0 | ✅ |
| Crashes | 0 | ✅ |
| Benchmark regression (G vs D) | G ≥ D all metrics | ✅ |

### Perceptual Proxy Gate (Automated)

3 independent blinded passes (signal analysis, sealed label key, no strategy names).

| Check | Result | Status |
|-------|--------|--------|
| G severe artifact count | 0 | ✅ |
| G overall mean vs D | 2.52 vs 2.59 (within 0.25 margin) | ✅ |
| G per-clip losses vs D (≤2) | 1 | ✅ |
| Listener agreement (std dev) | 0.03 | ✅ excellent |

**Perceptual gate: PASS**

### Production Smoke Test

AU and VST3 plugin targets build clean from merged main code. Zero compiler errors.

---

## Evidence Summary

| Corpus | Runs | Result |
|--------|------|--------|
| Torture suite (35 cases × 3 strategies) | 105 | Zero gate failures |
| Generated vocal corpus (93 clips × 3 strategies) | 279 | Zero failures, no systematic weakness |
| Adversarial search (43 scenarios) | 43 | No gate failure; failure boundary mapped |
| Interruption robustness (16 scenarios) | 16 | All pass ≤45c gate |
| Real-vocal blind test (10 clips × X/Y/Z × 3 passes) | 90 | Gate PASS, 0 severe artifacts |
| **Total bench runs** | **533** | **Zero blocking issues** |

---

## Known Accepted Behaviors (Unchanged)

| Behavior | Severity | Notes |
|----------|----------|-------|
| T31 ±550c vibrato: 56.11c | Low | Physically unrealistic signal; documented |
| T16 250ms notes: chatter 0.1264 | Low | TRANSITION-dominant by design |
| Perceptual proxy: G mean 0.07 pts below D | Low | Mechanistic: G's faster IIR registers as more pitch variation, which is the correction (pitch arrives faster) |

---

## Rollback

```bash
git revert 6c022cb   # restores pre-fix transitionBoost behavior
```

T10 would return to ~69c (above gate). No other metrics would regress.

---

## Final Verdict

**MERGED — STRATEGY G**

All hard gates pass. Zero severe perceptual artifacts. No systematic weakness across 533 total bench runs. AU and VST3 build clean. Rollback commit preserved.
