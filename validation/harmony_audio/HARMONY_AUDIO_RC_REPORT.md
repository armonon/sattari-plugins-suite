# Harmony Audio Engine — RC Report

## Architecture
- Per-voiced-frame pitch-shifting via RubberBand CLI
- Interval computed by HarmonyEngine (scale-aware, diatonic)
- Pitch-shift semitones = target_midi - detected_midi
- Formant preservation: --formant flag on all voices
- Pan law: constant-power stereo positioning
- Dry melody preserved at center; voices added

## Vocal Test Matrix

| Case | Voiced | Rendered | VL dist | Crash | NaN | OK? |
|------|--------|----------|---------|-------|-----|-----|
| C_major_scale | 690 | 300.0% | 1.7¢ | 0 | 0 | ✅ |
| A_minor_scale | 689 | 300.0% | 1.7¢ | 0 | 0 | ✅ |
| G_major_chords | 549 | 300.0% | 13.5¢ | 0 | 0 | ✅ |
| E_minor_chords | 549 | 300.0% | 13.5¢ | 0 | 0 | ✅ |
| vibrato_A4 | 192 | 300.0% | 154.6¢ | 0 | 0 | ✅ |
| slide_A3_A4 | 344 | 300.0% | 3.5¢ | 0 | 0 | ✅ |
| slide_A4_A3 | 345 | 300.0% | 3.5¢ | 0 | 0 | ✅ |
| fast_run_Amaj | 82 | 300.0% | 14.8¢ | 0 | 0 | ✅ |
| register_low_E2 | 343 | 300.0% | 0.0¢ | 0 | 0 | ✅ |
| register_hi_C5 | 345 | 300.0% | 0.0¢ | 0 | 0 | ✅ |
| chord_change_Cmaj_Amin | 345 | 300.0% | 12.1¢ | 0 | 0 | ✅ |
| inversion_E4_C4 | 345 | 300.0% | 13.0¢ | 0 | 0 | ✅ |
| chromatic_pass | 414 | 300.0% | 1.7¢ | 0 | 0 | ✅ |
| held_across_chord | 517 | 300.0% | 0.0¢ | 0 | 0 | ✅ |

## Voice Count Scaling

| Config | Voiced | Rendered | VL dist | Crash | OK? |
|--------|--------|----------|---------|-------|-----|
| 2voice_3rd_5th | 192 | 200.0% | 154.5¢ | 0 | ✅ |
| 3voice_std | 192 | 300.0% | 154.6¢ | 0 | ✅ |
| 4voice_full | 192 | 400.0% | 155.0¢ | 0 | ✅ |

## 308-Clip Corpus Results
- Sample: 60/308  Rendered: 60  Crashes: 0  NaN: 0
- Whole-file pitch shift per interval: +3rd (+3st), +5th (+7st), -3rd (-3st)

## Summary
- Average render rate: 300.0%
- Average voice-leading distance: 16.7¢
- Total crashes: 0
- Total NaN/Inf: 0

## Gate
```
HARMONY AUDIO RC
```