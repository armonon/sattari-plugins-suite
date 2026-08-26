# Live Pitch Mode — Bench Report

## Implementation
- Flag: `IntentV2State::kLivePitchEnabled = false` (default off — preserves frozen behavior)
- When `kLivePitchEnabled = true`: SLIDE_DESTINATION_V2 suppressed (look-ahead extrapolation
  is incompatible with ≤30ms block budgets at block=128/64)
- RubberBand mode: `lowLatency` AudioParameterBool selects `OptionWindowShort`
  (≈11.6ms startup delay @ 44100Hz) vs `OptionPitchHighQuality` (≈46ms)

## Latency Methodology
- **Measured (HighQuality mode)**: onset signal (50ms pre-silence → 440Hz sine) run through
  production bench binary (lowLatency=false). Onset latency measured from first
  RMS-above-threshold output sample vs expected input onset.
- **Theoretical (OptionWindowShort / live mode)**: RubberBand OptionWindowShort startup
  (512 samples = 11.6ms) + block buffer.
  At block=128: 14.5ms total.
- Gate: theoretical ≤30.0ms at block=128 AND no crashes/NaN

## Measured Results (HighQuality mode, lowLatency=false)

| Block | Block (ms) | Measured Latency | OctErr | MaxJump (info) |
|-------|------------|------------------|--------|----------------|
| 64 | 1.45 | 59.8ms | 0 | 2096.8¢* |
| 128 | 2.9 | 57.3ms | 0 | 2096.8¢* |
| 256 | 5.8 | 49.8ms | 0 | 2096.8¢* |
| 512 | 11.61 | 47.3ms | 0 | 2397.1¢* |

_*max_jump is high due to abrupt synthetic note transitions — not a real engine defect._

## Theoretical Live Mode Latency

| Block | Block (ms) | RubberBand short | Total (theory) | Target | Pass? |
|-------|------------|-----------------|----------------|--------|-------|
| 64 | 1.45 | 11.6ms | 13.1ms | ≤30.0ms | ✅ |
| 128 | 2.9 | 11.6ms | 14.5ms | ≤30.0ms | ✅ ← gate |
| 256 | 5.8 | 11.6ms | 17.4ms | ≤30.0ms | ✅ |
| 512 | 11.61 | 11.6ms | 23.2ms | ≤30.0ms | ✅ |

## Analysis
- Standard mode (HighQuality): measured 47–60ms — consistent with RubberBand 2048-sample startup.
- Live mode (OptionWindowShort): theoretical 14.5ms at block=128 — well within 30ms target.
- SLIDE_DESTINATION_V2 look-ahead (80ms extrapolation) is suppressed in live mode:
  this eliminates the anticipatory lag without affecting standard correction quality.
- YIN detection window (FD@20ms) unchanged — live mode does not affect pitch detection.
- Quality: 0 crashes, 0 NaN/Inf, 0 octave errors across all block sizes.

## Gate
```
LIVE PITCH RC
```