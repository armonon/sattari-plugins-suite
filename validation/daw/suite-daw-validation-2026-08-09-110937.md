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

- [ ] `ctest --test-dir build/local -L contract --output-on-failure`
- [ ] AU validation passes for every shipping Audio Unit.
- [ ] pluginval strictness 5 passes for every shipping VST3.
- [ ] Installer verification and Hub contract pass.
- [ ] Auto Pitch neural model reports embedded + loaded.

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

1.
2.
3.

## Decision

- [ ] Blocked
- [ ] Internal alpha
- [ ] Closed beta
- [ ] Release candidate

Rationale:
