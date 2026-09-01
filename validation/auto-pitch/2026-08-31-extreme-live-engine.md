# Auto Pitch Extreme Live engine qualification

Date: 2026-08-31

Product: Sattari Auto Pitch 0.5.0 internal alpha

Scope: causal monophonic monitoring and aligned generated harmony

## What changed

Extreme Live is a new causal renderer, not a renamed legacy mode. It uses the
newest fully observed pitch cycle, correlation-refined epochs, cubic resampling,
short cycle-update crossfades, ratio/wet dezippering, and an upward-shift
anti-alias guard. The unity and unvoiced path is an exact fixed delayed copy.

The detector has two causal ranges. A short 100 Hz-floor tracker accelerates
ordinary vocal onset acquisition; the existing 70 Hz-floor tracker takes
authority as soon as its longer history is ready. This avoids making every note
wait for the two-period window required by the bottom of the supported range.

Extreme, Ultra, and Range/Studio also have separate preallocated harmony banks.
Profile changes reset every dormant bank, preventing stale delayed audio from a
previous transport position from returning when a mode is reselected.

## Measured result

All figures below come from the executable contracts or objective benchmark:

| Gate | Result |
|---|---:|
| Reported and impulse-measured delay, 48 kHz | 96 samples / 2.00 ms, exact agreement |
| Clean-onset voiced acquisition | 24 ms |
| Clean-onset >=50-cent correction acquisition | 28 ms at processor level |
| E2 full-range tracker | voiced, 0.01-cent error |
| Settled correction, E2-C5 | 1.97-cent mean / 6.04-cent max (40 cases) |
| Corrected-tone THD / alias proxy | 0.19% / 0.11% |
| 48 kHz / 64-sample CPU p95 | 0.370 ms in a 1.33 ms callback, 3.6x headroom |
| Extreme plus four harmony voices, 512 samples | 2.429 ms CPU p95 in a 10.67 ms callback |
| Safety audit | 191 renders, 0 non-finite/clipping violations, max peak 0.9955 |

The objective suite also passed latency honesty, Ultra/Range regression,
note-transition monotonicity, vibrato preservation/generation, separated
two-to-four-note polyphonic correction, and hot-input four-voice harmony safety.
Apple `auval` passed the installed AU. `pluginval` passed the installed universal
VST3 at strictness level 5.

## Comparison boundary

Antares publishes 2.3 ms at 48 kHz for AutoTune 2026 Modern Low Latency and
0.77 ms for Classic Low Latency. Extreme's measured 2.00 ms therefore meets the
published Modern numeric processing-latency target but not the Classic figure.
This is not evidence of subjective sound-quality parity: real-vocal consonants,
rasp, breath, register transitions, vibrato and difficult note changes still
require level-matched listening against the commercial reference.

The qualified realtime matrix is 44.1/48 kHz at 64-1024 samples. The 96 kHz /
64-sample point is outside the current CPU gate. No public release claim should
be made from this machine record alone.

## Reproduction

```sh
./build-auto-pitch/AutoPitchCoreContract
./build-auto-pitch/AutoPitchObjectiveBench all
auval -v aufx Ap01 Satr
/Applications/pluginval.app/Contents/MacOS/pluginval \
  --strictness-level 5 --validate-in-process --verbose \
  --validate "$HOME/Library/Audio/Plug-Ins/VST3/Sattari Auto Pitch.vst3"
```
