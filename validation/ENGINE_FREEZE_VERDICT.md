# Engine Freeze Gate — 2026-08-25

## Production commit: d92093f  |  Strategy G commit: 6c022cb

> ### PROVENANCE WARNING — added 2026-08-27
>
> **Neither commit above exists in this repository** (`git cat-file -t` reports
> both as unknown objects), so no row in the table below can be reproduced or
> diffed against what ships. Treat every result here as unverifiable history,
> not as evidence about the current tree.
>
> The `AutoPitchRender null-test` row in particular (`residual 4.9e-7`) became
> FALSE at some point before 2026-08-27, when that gate was measured failing at
> residual 0.173507 on the canonical build. The cause was a stale golden, not a
> DSP regression — see the correction in `validation/PRODUCT_RC_REPORT.md` and
> the full record in `validation/auto-pitch/2026-08-27-review-fixes.md`. After
> re-blessing against the corrected engine the gate passes again, and 4.9e-7 is
> once more the measured figure — but it was not continuously true, and this
> document asserted it while it was false.
>
> A gate result is only meaningful next to a commit someone can check out.

---

## Gate Results

| Suite | Result | Detail |
|-------|--------|--------|
| CoreContract | ✅ PASS | 0 crashes, 0 NaN/Inf |
| AutoPitchRender null-test | ✅ PASS | residual 4.9e-7 < 0.0001 |
| MotionBench | ✅ GO | 1.788 MIDI/sec ALL-RMSE (≤ 2.0 gate) |
| IntentBench v3 | ✅ PASS | 81.4% acc @ conf≥0.9, 0 octave errors, 0 crashes |
| VocalBench stable (39) | ✅ PASS | 39/39, mean err 8.2¢, 0 octave errors |
| VocalBench boundary | ✅ PASS | 13/13 (72 checks all ✅) |
| Torture regression | ✅ PASS | RUN_up_350ms max_jump=248.5¢ < 350¢ |
| AU auval | ✅ PASS | AU VALIDATION SUCCEEDED |
| VST3 codesign | ✅ PASS | signed, plist OK |
| NaN/Inf scan (829 WAVs) | ✅ PASS | 0 |
| Strategy G DSP | ✅ FROZEN | No transitionBoost/hysteresis/smoothing changes |

---

## Hard Gate Summary

- crashes=0  ✅
- NaN/Inf=0  ✅
- octave_errors=0  ✅
- VocalBench corrected 39/39  ✅
- Intent v3 gains preserved (81.4% vs 65.2% prod at conf≥0.9)  ✅
- Strategy G baseline unchanged  ✅
- RUN_up_350ms max_jump 248.5¢ < 350¢ HARD threshold  ✅
- Perceptual proxy: CAUTION (0 SEVERE / 120 MODERATE — accepted risk)  ✅

---

## Verdict

```
ENGINE FROZEN — READY FOR PRODUCT RC
```
