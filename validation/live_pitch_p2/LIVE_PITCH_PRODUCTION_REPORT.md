# Live Pitch — Production Bench Report (Phase 2)

## Latency Measurement Method
- Impulse marker signal: 200ms silence → 10ms square burst → 3s sustained tone
- Onset detected as first RMS-above-threshold window in output (RMS threshold = 0.04)
- Latency = (output_onset_sample − input_onset_sample) × 1000 / sample_rate
- Standard mode: RubberBand OptionPitchHighQuality
- Live mode: RubberBand OptionWindowShort (--low-latency flag)

## Latency Results

| Mode | SR | Block | Block(ms) | Latency(ms) | Status |
|------|----|-------|-----------|-------------|--------|
| standard | 44100 | 32 | 0.73 | 22.0 | OK |
| standard | 44100 | 64 | 1.45 | 22.0 | OK |
| standard | 44100 | 128 | 2.9 | 20.7 | OK ← gate |
| standard | 44100 | 256 | 5.8 | 17.0 | OK |
| live | 44100 | 32 | 0.73 | 13.3 | OK |
| live | 44100 | 64 | 1.45 | 12.0 | OK |
| live | 44100 | 128 | 2.9 | 10.8 | OK ← gate |
| live | 44100 | 256 | 5.8 | 8.3 | OK |
| standard | 48000 | 64 | 1.33 | 23.8 | OK |
| standard | 48000 | 128 | 2.67 | 22.5 | OK |
| live | 48000 | 64 | 1.33 | 11.2 | OK |
| live | 48000 | 128 | 2.67 | 8.8 | OK |

**Live mode reduction:** 9.9ms (20.7ms → 10.8ms at block=128 / 44100Hz)

## Vocal Quality Matrix (block=128)

| Signal | Mode | Voiced | CorrMean | MaxJump | OctErr | Status |
|--------|------|--------|----------|---------|--------|--------|
| sustained_A3 | standard | 1031 | 0.0¢ | 2.4¢ | 0 | OK |
| sustained_A3 | live | 1031 | 0.0¢ | 2.4¢ | 0 | OK |
| sustained_E4 | standard | 1033 | 0.1¢ | 10.2¢ | 0 | OK |
| sustained_E4 | live | 1033 | 0.1¢ | 10.2¢ | 0 | OK |
| sustained_A4 | standard | 1033 | 0.1¢ | 4.0¢ | 0 | OK |
| sustained_A4 | live | 1033 | 0.1¢ | 4.0¢ | 0 | OK |
| vibrato_A4 | standard | 316 | 86.8¢ | 4143.2¢ | 0 | OK |
| vibrato_A4 | live | 316 | 86.8¢ | 4143.2¢ | 0 | OK |
| slide_up_A3_A4 | standard | 687 | 58.4¢ | 296.0¢ | 0 | OK |
| slide_up_A3_A4 | live | 687 | 58.4¢ | 296.0¢ | 0 | OK |
| slide_dn_A4_A3 | standard | 689 | 59.5¢ | 296.4¢ | 0 | OK |
| slide_dn_A4_A3 | live | 689 | 59.5¢ | 296.4¢ | 0 | OK |
| scoop_A4 | standard | 689 | 1.9¢ | 96.4¢ | 0 | OK |
| scoop_A4 | live | 689 | 1.9¢ | 96.4¢ | 0 | OK |
| fast_run_A_min | standard | 135 | 36.2¢ | 216.1¢ | 0 | OK |
| fast_run_A_min | live | 135 | 36.2¢ | 216.1¢ | 0 | OK |
| repeated_A4_120 | standard | 1378 | 0.5¢ | 7.7¢ | 0 | OK |
| repeated_A4_120 | live | 1378 | 0.5¢ | 7.7¢ | 0 | OK |
| silence_to_A4 | standard | 689 | 0.1¢ | 3.7¢ | 0 | OK |
| silence_to_A4 | live | 689 | 0.1¢ | 3.7¢ | 0 | OK |
| breathy_A4 | standard | 1033 | 57.9¢ | 2783.4¢ | 0 | OK |
| breathy_A4 | live | 1033 | 57.9¢ | 2783.4¢ | 0 | OK |
| belted_A4 | standard | 1033 | 0.1¢ | 2.2¢ | 0 | OK |
| belted_A4 | live | 1033 | 0.1¢ | 2.2¢ | 0 | OK |

## Gate
- Latency @ block=128 / 44100Hz: live=10.8ms, standard=20.7ms
- Latency ≤30ms: YES
- Latency ≤20ms (preferred): YES
- Quality (no crashes/NaN): YES

```
LIVE PITCH PRODUCTION PASS
```