---
type: validation-evidence
product: Sattari Auto Pitch
version: 0.5.0 RC3
date: 2026-08-16
status: PASS_FOR_USER_LISTENING
truth_label: objective local evidence; human listening remains open
---

# RC3 fast-correction hardening

## Release blocker found

The first direct Classic/Hard feature gate exposed a real timing defect. A
controlled A4 tone with 5.5 Hz, +/-40-cent vibrato measured 19.7 cents of pitch
deviation at the input but 27.3 cents after Hard correction. The shifter was
receiving the requested correction magnitude, but the moving control and R3
analysis response were out of phase. The prior corpus nearest-note statistic did
not identify the cause and is explicitly invalid for intentional vibrato.

## Engine correction

- R3/Finer now uses its short window, which Rubber Band documents as the faster,
  lower-delay R3 mode suitable for non-percussive material.
- On agreeing voiced frames, CREPE now validates YIN confidence without averaging
  its slower 64 ms contour into YIN. CREPE still rescues high-confidence frames
  when YIN is unvoiced and remains subject to continuity gating.
- Classic mode offsets only the moving component of correction by a measured
  -30 ms. The term is capped at 75 cents, disabled at fresh note transitions,
  and is exactly zero for steady pitch.
- Neural-mode vibrato preservation now follows the fused contour actually used;
  the obsolete 0.40 attenuation from the former averaged contour was removed.

Calibration sweep on the controlled tone:

```text
control offset       Hard pitch deviation
  0 ms               15.8 cents
 -5 ms               13.1 cents
-10 ms               10.3 cents
-15 ms                7.3 cents
-20 ms                5.1 cents  PASS
-30 ms                4.0 cents  PASS / selected
-40 ms                8.6 cents  FAIL
```

The selected result suppresses about 80% of the controlled vibrato motion.
Natural preservation measures 15.4 cents against 19.7 input and remains
recognizably expressive.

## Real-vocal spot checks

On the sustained VocalSet belt, the old RC2 versus RC3 nearest-chromatic metrics
were:

```text
                    RC2 median        RC3 median
Natural             10.0 ct / 75%      6.8 ct / 83%
Hard                10.6 ct / 73%      2.3 ct / 94%
```

Both RC3 renders remain finite and unclipped. The intentional-vibrato clip's
nearest-note statistic improved but remains a secondary, musically invalid metric;
the controlled modulation test above is the release gate for flattening behavior.

## Host timing

R3 Short's forced-unity onset measures 2,848 samples and a genuinely tuned onset
measures 2,922 at 48 kHz / 512. RC3 reports their midpoint, 2,885 samples
(60.10 ms): both reconstructed paths are within 37 samples / 0.77 ms of host PDC.
Five untouched dry and zero-Amount cases null at negative infinity with zero
sample offset.

## Remaining boundary

This is sufficient to begin structured user listening, not evidence of Antares
or BandLab subjective parity. Human scores, consonant/formant review, realtime vs
offline comparison, and manual DAW host matrices remain required before release.
