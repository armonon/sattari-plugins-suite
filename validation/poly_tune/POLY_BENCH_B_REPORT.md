# Polyphonic Analysis — Phase B Report

**Method:** Spectral Salience (librosa.salience) + harmonic suppression
**n_fft:** 8192  **SR:** 44100  **Max voices:** 6
**conf_threshold:** 0.04  **suppress_ratio:** 0.40

## Results

| Case | GT Notes | Detected | TP | FP | FN | Cents Err |
|------|----------|----------|----|----|----|-----------|
| ✅ interval_maj3_C4E4 | [60, 64] | 2/2 | 2 | 0 | 0 | 10.5¢ |
| ✅ interval_min3_A3C4 | [57, 60] | 2/2 | 2 | 0 | 0 | 10.0¢ |
| ✅ interval_p5_C4G4 | [60, 67] | 2/2 | 2 | 0 | 0 | 9.0¢ |
| ✅ interval_tritone_C4F#4 | [60, 66] | 2/2 | 2 | 0 | 0 | 10.5¢ |
| ✅ interval_oct_C3C4 | [48, 60] | 2/2 | 2 | 0 | 0 | 17.5¢ |
| ✅ interval_maj6_C4A4 | [60, 69] | 2/2 | 2 | 0 | 0 | 10.0¢ |
| ✅ interval_min7_C4Bb4 | [60, 70] | 2/2 | 2 | 0 | 0 | 11.0¢ |
| ✅ chord_Cmaj | [60, 64, 67] | 3/3 | 3 | 0 | 0 | 8.3¢ |
| ✅ chord_Amin | [57, 60, 64] | 3/3 | 3 | 0 | 0 | 9.0¢ |
| ✅ chord_Gmaj | [55, 59, 62] | 3/3 | 3 | 0 | 0 | 13.0¢ |
| ✅ chord_Emin | [52, 55, 59] | 3/3 | 3 | 0 | 0 | 15.7¢ |
| ✅ chord_Fmaj | [53, 57, 60] | 3/3 | 3 | 0 | 0 | 14.3¢ |
| ✅ chord_dom7_G7 | [55, 59, 62, 65] | 4/4 | 4 | 0 | 0 | 10.5¢ |
| ✅ chord_Cmaj7 | [60, 64, 67, 71] | 4/4 | 4 | 0 | 0 | 7.5¢ |
| ✅ chord_Amin7 | [57, 60, 64, 67] | 4/4 | 4 | 0 | 0 | 7.8¢ |
| ✅ chord_dom7_C7 | [60, 64, 67, 70] | 4/4 | 4 | 0 | 0 | 8.3¢ |
| ✅ chord_half_dim_B | [59, 62, 65, 69] | 4/4 | 4 | 0 | 0 | 7.0¢ |
| ✅ power_A2A3E3 | [45, 52, 57] | 3/3 | 3 | 0 | 0 | 21.7¢ |
| ⚠ power_E2B2E3 | [40, 47, 52] | 2/3 | 2 | 0 | 1 | 13.5¢ |
| ✅ pad_Cmaj_close | [60, 64, 67] | 3/3 | 3 | 0 | 0 | 8.3¢ |
| ✅ pad_Amin_close | [57, 60, 64] | 3/3 | 3 | 0 | 0 | 9.0¢ |
| ✅ piano_Cmaj_open | [48, 64, 67] | 3/3 | 3 | 0 | 0 | 10.7¢ |
| ✅ piano_Gmaj_open | [43, 59, 62] | 3/3 | 3 | 0 | 0 | 13.0¢ |
| ✅ mono_A4 | [69] | 1/1 | 1 | 0 | 0 | 6.0¢ |
| ✅ mono_C4 | [60] | 1/1 | 1 | 0 | 0 | 14.0¢ |
| ✅ mono_E3 | [52] | 1/1 | 1 | 0 | 0 | 22.0¢ |
| ✅ chord_Dmin7_wide | [50, 57, 62, 69] | 4/4 | 4 | 0 | 0 | 11.0¢ |
| ✅ chord_Cmaj_wide | [48, 60, 67] | 3/3 | 3 | 0 | 0 | 13.0¢ |
| ✅ interval_p5_mid_E4B4 | [64, 71] | 2/2 | 2 | 0 | 0 | 6.0¢ |
| ✅ interval_maj3_mid_G4B4 | [67, 71] | 2/2 | 2 | 0 | 0 | 4.5¢ |
| ✅ interval_min3_mid_A4C5 | [69, 72] | 2/2 | 2 | 0 | 0 | 5.0¢ |

## Summary
- **Precision:** 1.000 (82/82) ✅ (gate ≥0.93)
- **Recall:** 0.988 (82/83) ✅ (gate ≥0.85)
- **Chord accuracy:** 0.968 (30/31 exact)

## Gate
```
POLY TUNE BETA
```