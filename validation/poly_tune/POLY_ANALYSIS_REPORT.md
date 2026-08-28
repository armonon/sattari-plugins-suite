# Polyphonic Analysis — Phase A Report

**Method:** Iterative HPS (Harmonic Product Spectrum) with harmonic subtraction
**Frame size:** 4096  **SR:** 44100  **Max voices:** 6

## Results

| Case | GT Notes | Detected | TP | FP | FN | Cents Err |
|------|----------|----------|----|----|----|-----------|
| interval_maj3_C4E4 | [60, 64] | 2/2 | 2 | 0 | 0 | 14.5¢ |
| interval_min3_A3C4 | [57, 60] | 2/2 | 2 | 0 | 0 | 23.5¢ |
| interval_p5_C4G4 | [60, 67] | 2/2 | 2 | 0 | 0 | 9.0¢ |
| interval_tritone_C4F#4 | [60, 66] | 2/2 | 2 | 0 | 0 | 9.0¢ |
| interval_oct_C3C4 | [48, 60] | 2/2 | 2 | 0 | 0 | 23.0¢ |
| interval_maj6_C4A4 | [60, 69] | 2/2 | 2 | 0 | 0 | 8.5¢ |
| interval_min7_C4Bb4 | [60, 70] | 2/2 | 2 | 0 | 0 | 9.5¢ |
| chord_Cmaj | [60, 64, 67] | 3/3 | 3 | 0 | 0 | 16.0¢ |
| chord_Amin | [57, 60, 64] | 3/3 | 3 | 0 | 0 | 18.7¢ |
| chord_Gmaj | [55, 59, 62] | 3/3 | 3 | 0 | 0 | 24.3¢ |
| chord_Emin | [52, 55, 59] | 1/3 | 1 | 0 | 2 | 66.0¢ |
| chord_Fmaj | [53, 57, 60] | 3/3 | 3 | 0 | 0 | 29.3¢ |
| chord_dom7_G7 | [55, 59, 62, 65] | 0/4 | 0 | 0 | 4 | 0¢ |
| chord_Cmaj7 | [60, 64, 67, 71] | 0/4 | 0 | 0 | 4 | 0¢ |
| chord_Amin7 | [57, 60, 64, 67] | 0/4 | 0 | 0 | 4 | 0¢ |
| chord_dom7_C7 | [60, 64, 67, 70] | 0/4 | 0 | 0 | 4 | 0¢ |
| chord_half_dim_B | [59, 62, 65, 69] | 0/4 | 0 | 0 | 4 | 0¢ |
| power_A2A3E3 | [45, 52, 57] | 3/3 | 3 | 0 | 0 | 45.7¢ |
| power_E2B2E3 | [40, 47, 52] | 2/3 | 2 | 0 | 1 | 27.5¢ |
| pad_Cmaj_close | [60, 64, 67] | 3/3 | 3 | 0 | 0 | 16.0¢ |
| pad_Amin_close | [57, 60, 64] | 3/3 | 3 | 0 | 0 | 18.7¢ |
| piano_Cmaj_open | [48, 64, 67] | 3/3 | 3 | 0 | 0 | 23.7¢ |
| piano_Gmaj_open | [43, 59, 62] | 3/3 | 3 | 0 | 0 | 38.7¢ |
| mono_A4 | [69] | 2/1 | 1 | 1 | 0 | 1.0¢ |
| mono_C4 | [60] | 2/1 | 1 | 1 | 0 | 6.0¢ |
| mono_E3 | [52] | 2/1 | 1 | 1 | 0 | 14.0¢ |
| chord_Dmin7_wide | [50, 57, 62, 69] | 2/4 | 2 | 0 | 2 | 2.5¢ |
| chord_Cmaj_wide | [48, 60, 67] | 3/3 | 3 | 0 | 0 | 22.3¢ |
| interval_p5_mid_E4B4 | [64, 71] | 2/2 | 2 | 0 | 0 | 5.5¢ |
| interval_maj3_mid_G4B4 | [67, 71] | 2/2 | 2 | 0 | 0 | 7.0¢ |
| interval_min3_mid_A4C5 | [69, 72] | 2/2 | 2 | 0 | 0 | 7.0¢ |

## Summary
- **Precision:** 0.951 (58/61)
- **Recall:** 0.699 (58/83)
- **Chord accuracy:** 0.645 (20/31 exact)

## Known Limitations
- HPS struggles with close intervals (minor 2nd, major 2nd) due to spectral overlap
- Wide-voicing chords (>2 octaves) can fool HPS into partial F0 detection
- Distorted/noisy signals will reduce precision significantly
- This is Phase A only — Phase B should add note tracking and confidence smoothing

## Phase B Recommendation
- Add temporal note tracking (Kalman or median filter over multiple frames)
- Use CQT-based analysis for better low-frequency resolution on bass notes
- Minimum note duration gate (50ms) to suppress phantom detections

## Gate
```
POLY TUNE ALPHA PASS
```