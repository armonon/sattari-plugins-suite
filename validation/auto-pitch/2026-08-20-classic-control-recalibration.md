---
type: calibration-finding
product: auto-pitch
date: 2026-08-20
truth_label: internal validation only; nothing here is a shipping claim
status: RESOLVED — a constant tuned on one synthetic tone, corrected on the real corpus
---

# Hard mode corrected LESS than Natural, and why

On `vs-vibrato-f5` the Hard render tightened **25%** against Natural's **34%** —
Hard is the deliberately dead-flat path, so correcting less than the
vibrato-preserving one is an inversion of what the control means.

## Cause

`classicControlOffsetSeconds` compensates R3's finite analysis response by
leading the MOVING part of the requested correction curve — bounded to ±75
cents. Its value was selected as **−30 ms on one controlled 5.5 Hz / ±40-cent
synthetic tone**, where −30 is genuinely optimal (4.0 ct remaining vs 5.1 at −20).

Real vibrato does not have that rate and depth. On material that differs, the
lead **over-shoots** — pushing the correction past the target — and the error it
adds can exceed the error it removes.

## The sweep

Hard-mode cents-to-scale, median, on the whole six-clip corpus:

| clip | 0 ms | −10 | −15 | **−20** | −30 |
|---|---:|---:|---:|---:|---:|
| slay-demons | 16.2 | 12.1 | 9.4 | **7.4** | 8.7 |
| guidos-pizza | 11.5 | 7.6 | 5.7 | **4.2** | 4.6 |
| dickey-prairie | 8.4 | 6.4 | 6.3 | **6.1** | 6.3 |
| vs-belt-m6 | 5.8 | 4.4 | 3.7 | 2.7 | **2.3** |
| vs-vibrato-f5 | 18.5 | 14.6 | **13.7** | 14.1 | 16.7 |
| vs-melisma-m3 | 21.3 | 15.6 | 13.3 | **11.1** | 11.4 |

**−20 ms wins 4 of 6 outright**, and wins on both mean (7.60 vs 8.33) and median
(6.75 vs 7.50).

## Why NOT −15, which scored best on the clip that exposed the bug

−15 fails the controlled release gate: **7.3 ct against its 6.9 ct limit**
(35% of the 19.7 ct input). Choosing it would have fixed the symptom by tuning
to the single clip that revealed it — the original error, inverted. −20 passes
that gate with margin at 5.1 ct.

This is the third time in this project a constant chosen on one favourable
dataset failed to generalise (the neural key model, the Sha'ath profile). The
pattern is worth naming: **a calibration is only as good as the variety of the
material it was calibrated on.**

## Result

Every clip now has Hard ≥ Natural, as the control implies:

```
slay-demons     45% -> 69%      vs-belt-m6      82% -> 93%
guidos-pizza    58% -> 81%      vs-vibrato-f5   34% -> 37%
dickey-prairie  58% -> 69%      vs-melisma-m3   35% -> 54%
```

Controlled gate still passes (5.1 ct). Regression: nulltest 8/8 `-inf`,
livetest, followmap, smoke, core contract, wetalign 69 samples.

The corpus was re-rendered and the listening package regenerated, because the
DSP changed after the first package was built.
