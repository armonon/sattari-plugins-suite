# Sattari Align

Truth label: **internal alpha**. Builds AU + VST3 + Standalone.

Sattari Align time-aligns the **Main** signal to the **Guide** sidechain input via
onset-envelope cross-correlation. It is signal-agnostic — vocal doubles, guitar
layers, drum stems, stereo pairs, anything with transients. v1 is bulk-delay
alignment; pitch-and-time DTW warping is a separate (not-yet-shipped) v2.

## Routing

1. Put **Sattari Align** on the track you want shifted (the *target*).
2. Route the *reference* track to the plugin's **Guide** sidechain input
   (Ableton: "Audio To" → the Align track + select `Sidechain`; Logic / FL /
   Reaper: same idea, "Sidechain Input" on the plugin slot).
3. Play. The big readout shows the detected offset in ms, signed (`+` means the
   plugin is delaying the Main further; `−` means it would need to advance,
   capped at zero in realtime).

## How it works

* Rectified differentiated energy of both signals → smoothed onset envelopes,
  decimated to 200 Hz.
* Normalised cross-correlation across the last 3 s of envelope, searching ± the
  user-set range, runs every ~200 ms.
* Detected lag is low-pass smoothed (Smoothing knob) and applied as a fractional
  delay on the Main path. The plugin reports its half-search-range as host
  latency, so DAW PDC compensates around the stable centre and the detected
  lag can swing both negative and positive.

## Controls

| Control | Range / Effect |
|---|---|
| **Auto Align** | When on, the detected lag follows the Guide automatically. |
| **Lock** | Freezes the current detected lag (good for printing aligned takes). |
| **Manual Offset** | When Auto Align is off (or Lock is on), the lag is steered manually. |
| **Search Range** | Maximum lag the cross-correlator will consider. Wider = more headroom but slower convergence. |
| **Sensitivity** | Lower = require stronger correlation to act on a detection. |
| **Smoothing** | Higher = slower, steadier lag updates. |
| **Bypass** | Passes Main → Output through the same delay line so the host PDC chain stays sample-aligned. |

## Limits (be honest)

* **Bulk shift only.** If the takes drift apart over the song, v1 can only pick
  one lag; it will not warp.
* **Realtime.** We can delay Main to match Guide but not advance it. If Main is
  earlier than Guide, the lag is clamped to zero — route the *other* signal to
  Guide if you need the opposite direction.
* **Quiet material.** When the Guide bus is silent we hold the last lag.

## Validate (machine gate)

```bash
cmake -S plugins/align -B build/align -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/align -j

scripts/validate_plugin.sh build/align/SattariAlign_artefacts/Release aufx Algn Satr
```

This proves codesign, auval (AU), pluginval@5 (VST3) all pass. **It does not
prove the alignment sounds correct on real material** — DAW listening is still
required.
