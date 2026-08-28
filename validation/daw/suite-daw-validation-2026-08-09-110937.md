# Sattari suite DAW validation — 2026-08-09-110937

Truth label: **manual release gate / unchecked rows are not passes**.

## Environment

- Commit: 225c1888073350bc3eff5a133a8a2988d7d5ac7e
- macOS: 26.5.1
- Machine:       Model Name=MacBook Air;       Chip=Apple M3;       Memory=24 GB; 
- Logic Pro Creator Studio: installed
- Ableton Live 12 Suite: installed
- FL Studio 2026: installed
- Interface / driver:
- Tester:

## Machine gates

- [x] `ctest --test-dir build/local -L non-stemdeck --output-on-failure` — 91/91 passed in 174.11 seconds.
- [x] AU validation passes for every shipping Audio Unit.
- [x] pluginval strictness 5 passes for every shipping VST3.
- [x] Installer expanded successfully; packaged Hub contract passed with six suites, 48 implementation plugins, and 143 bundle records.
- [x] Auto Pitch neural bundles passed embedded-model/runtime verification; AU passed auval and VST3 passed pluginval strictness 5.

Compass AU also passed auval as `aumf/Br01/Satr`, and Compass VST3 passed
pluginval strictness 5. Fresh Release and packaged-copy validation now also
passes for Arp (`aumi/Arp1/Satr`, VST3) and Sub Conjurer
(`aumf/Sc01/Satr`, VST3, Standalone). These are machine gates only; the host
matrix below remains open.

## Host matrix

Repeat at 44.1, 48, and 96 kHz. Use 64/128/256/512 buffers where the host allows it.

| Host | Format | Scan | Insert | Audio/MIDI | Automation | Save/reopen | Offline bounce | 4 instances | Session isolation | Verdict / notes |
|---|---|---|---|---|---|---|---|---|---|---|
| Logic Pro Creator Studio | AU | | | | | | | | | |
| Ableton Live 12 Suite | AU | | | | | | | | | |
| Ableton Live 12 Suite | VST3 | | | | | | | | | |
| FL Studio 2026 | AU | | | | | | | | | |
| FL Studio 2026 | VST3 | | | | | | | | | |

## Product flows

- [ ] Compass publishes key, scale, reference, chord, section, energy, swing, and time signature.
- [ ] Key follows Compass and manual override works.
- [ ] Auto Pitch follows session/key correctly; Natural and Hard remain distinct.
- [ ] Arp, Sub Conjurer, Create, Mix, Vocal, and Stack consume the same project session without cross-project bleed.
- [ ] Vocal Studio chain: Clean -> Gate -> De-Ess -> EQ -> Comp -> Heat -> Double -> Space.
- [ ] Every visible control automates, restores, and survives undo/redo in the host.
- [ ] Two DAWs launched with different `SATTARI_SESSION_ID` values do not exchange session state.
- [ ] Two instances in the same isolated project do exchange session state.

## Stress and sound-quality checks

- [ ] Auto Pitch has no crackle, metallic doubling, transient chirps, consonant smearing, or formant jumps.
- [ ] Realtime playback and offline bounce are perceptually consistent.
- [ ] Rapid transport start/stop, loop boundaries, sample-rate changes, and plugin removal do not crash.
- [ ] Automation at block boundaries does not click or produce NaN/Inf.
- [ ] CPU remains usable with four representative suite instances at the smallest supported buffer.
- [ ] UI scales, text remains legible, keyboard focus works, and no control is clipped.

## Issues

1. Manual Logic, Ableton, and FL Studio rows are not yet executed.
2. Real-vocal Auto Pitch listening corpus is not yet supplied or scored.
3. Developer ID Application/Installer certificates and the `sattari-notary` profile are absent.

## Decision

- [x] Blocked for public release; ready for controlled internal beta.
- [ ] Internal alpha
- [ ] Closed beta
- [ ] Release candidate

Rationale:
