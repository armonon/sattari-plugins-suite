# Adversarial / Red-Team QA Report

## Engine Attacks

| Test | Crash | NaN/Inf | OctErr | MaxJump | Voiced |
|------|-------|---------|--------|---------|--------|
| eng_silence_2s | no | 0 | 0 | 0.0¢ | 0 |
| eng_dc_pos | no | 0 | 0 | 0.0¢ | 0 |
| eng_dc_neg | no | 0 | 0 | 0.0¢ | 0 |
| eng_fullscale_p | no | 0 | 0 | 0.0¢ | 0 |
| eng_fullscale_n | no | 0 | 0 | 0.0¢ | 0 |
| eng_noise_only | no | 0 | 0 | 0.0¢ | 0 |
| eng_subharm_A3 | no | 0 | 0 | 3.5¢ | 515 |
| eng_clipped_E4 | no | 0 | 0 | 6.1¢ | 517 |
| eng_rapid_chromatic | no | 0 | 0 | 200.0¢ | 215 |
| eng_rapid_octave_jump | no | 0 | 0 | 1296.7¢ | 165 |
| eng_near_nyquist | no | 0 | 0 | 217.5¢ | 345 |
| eng_one_block_64 | no | 0 | 0 | 0.0¢ | 0 |

## Parameter Extremes

| Test | Crash | NaN/Inf | OctErr | Voiced |
|------|-------|---------|--------|--------|
| amount=0.0 | no | 0 | 0 | 517 |
| amount=1.0 | no | 0 | 0 | 517 |
| speed=0.01 | no | 0 | 0 | 517 |
| speed=1.0 | no | 0 | 0 | 517 |
| block=64 | no | 0 | 0 | 2065 |
| block=64+1.0 | no | 0 | 0 | 2065 |

## Scale Edge Cases

| Test | Crash | NaN/Inf | Voiced |
|------|-------|---------|--------|
| chromatic_Dmin | no | 0 | 517 |
| major_B | no | 0 | 517 |
| minor_Db | no | 0 | 517 |
| minor_G# | no | 0 | 517 |

## Harmony Engine Adversarial

| Test | Illegal | Crashes | OK? |
|------|---------|---------|-----|
| very_low_notes | 0 | 0 | ✅ |
| very_high_notes | 0 | 0 | ✅ |
| chromatic_full | 0 | 0 | ✅ |
| key_change_stress | 0 | 0 | ✅ |
| all_intervals_extreme | 0 | 0 | ✅ |
| fractional_midi | 0 | 0 | ✅ |
| stability_1000 | 0 | 0 | ✅ |
| all_roots | 0 | 0 | ✅ |

## Polyphonic Analyzer Adversarial

| Test | Crash | NaN/Inf | Voices |
|------|-------|---------|--------|
| silence | no | no | 0 |
| dc_offset | no | no | 0 |
| white_noise | no | no | 0 |
| full_scale | no | no | 0 |
| sub_20hz | no | no | 0 |
| 12_voices | no | no | 1 |
| subharmonic_A4 | no | no | 2 |
| near_nyquist | no | no | 0 |
| extreme_low_A0 | no | no | 1 |
| extreme_high_C8 | no | no | 0 |

## Summary
- Total test cases: 40
- Engine crashes: 0
- Engine NaN/Inf frames: 0
- Engine octave errors (subharmonic attacks): 0
- Harmony illegal tone failures: 0
- Poly analyzer crashes: 0
- Poly analyzer NaN/Inf: 0

## Notable Findings
- Silence, DC offset, white noise: engine stays silent (no voiced frames) — correct.
- Full-scale clipping (+1.0 / -1.0): engine processes without crash or NaN.
- Subharmonic signal (root + half frequency): engine may detect subharmonic but
  does NOT crash or produce NaN — octave classification is informational.
- Rapid chromatic succession (50ms/note): no crash, no NaN — Strategy G smoothing holds.
- Parameter extremes (amount=0, speed=0.01, block=64): all stable.
- Harmony engine: all roots × all scales (17×3=51 combinations) — 0 illegal tones.
- Poly analyzer: silence/DC/noise return 0 voices — correct behavior.

## Gate
```
ADVERSARIAL QA PASS
```