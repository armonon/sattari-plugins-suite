# Sattari VoxKey — Voice-to-MIDI Instrument

VoxKey is a real-time vocal audio → MIDI plugin for Ableton Live. It passes the dry vocal audio through untouched while detecting sung pitch and emitting MIDI notes that can drive a synth, piano, bass, pad, strings, or Drum Rack.

## Best build path

**MVP chosen: JUCE VST3/AU.**

Max for Live would be the fastest sketch, but a reliable M4L version usually becomes two devices: an audio tracker on the vocal track and a MIDI receiver on the instrument track. VoxKey is meant to become a real Sattari plugin, so JUCE is the better first product path: native AU/VST3 builds, reusable DSP modules, plugin validation, branded UI, and later signing/notarization.

## Architecture

```text
Mic / vocal audio in
  ↓
Sattari VoxKey audio effect
  ├─ dry vocal pass-through, unchanged
  ├─ VoxKeyPitchDetector: lightweight real-time YIN-style monophonic pitch tracker
  ├─ confidence + RMS gate to reject breath/noise
  ├─ VoxKeyVoiceMidiMapper: note stabilization, scale/key lock, velocity, modes
  ├─ MIDI event generator: note on/off, pitch bend, all notes off
  └─ performance UI: input orb, detected note, MIDI note, activity
  ↓
Dry vocal audio out + MIDI out
```

Core files:
- `src/PitchDetector.*` — low-latency vocal pitch frames.
- `src/VoiceMidiMapper.*` — pitch frame → musical MIDI behavior.
- `src/PluginProcessor.*` — DAW audio/MIDI routing and dry pass-through.
- `src/PluginEditor.*` — Sattari performance UI.
- `tests/voxkey_core_contract.cpp` — detector/mapper contract test.

## Current MVP features

- Dry vocal pass-through.
- Real-time monophonic vocal pitch detection.
- MIDI note output on channel 1.
- Velocity from input level.
- Note-on/note-off, hold, release, sensitivity, smoothing, confidence threshold.
- Scale lock: chromatic, major, minor, pentatonic.
- Key selector.
- Octave shift.
- Modes: Melody, Bass Follow, Harmony, Drum Mouth, Pad/Soul Layer.
- Optional pitch bend from vocal vibrato, assuming receiving instrument pitch-bend range is +/- 2 semitones.
- Glide toggle, bypass, MIDI panic.
- Visual input/midi orb, detected note, output note, confidence, MIDI activity.

## Build + validate

From `repos/sattari-plugins`:

```bash
./scripts/validate_voxkey.sh
```

Or manually:

```bash
cmake -S plugins/voxkey -B build/voxkey -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/voxkey --config Release -j 4
./build/voxkey/VoxKeyCoreContract
auval -v aufx Vk01 Satr
```

Build artefacts are under:

```text
repos/sattari-plugins/build/voxkey/SattariVoxKey_artefacts/Release/VST3/Sattari VoxKey.vst3
repos/sattari-plugins/build/voxkey/SattariVoxKey_artefacts/Release/AU/Sattari VoxKey.component
```

The JUCE build also copies local plugin installs to:

```text
~/Library/Audio/Plug-Ins/VST3/Sattari VoxKey.vst3
~/Library/Audio/Plug-Ins/Components/Sattari VoxKey.component
```

## Package

```bash
./scripts/package_voxkey_alpha.sh
```

This writes an internal alpha package under:

```text
deliverables/sattari-test-builds/voxkey-plugin-alpha-v0.1.0-ableton-mvp/
```

Truth label: internal MVP alpha, not sale-ready.

## Ableton Live setup

1. Open Live and rescan plugins if needed.
2. Create an **Audio Track** named `Vocal / VoxKey`.
3. Set `Audio From` to the microphone/interface input.
4. Set Monitor to `In`, or arm the track with Auto monitoring.
5. Drop **Sattari VoxKey** on this audio track. Prefer the VST3 first for Ableton routing.
6. Create a **MIDI Track** named `VoxKey Instrument`.
7. Load a synth, piano, bass, pad, strings, or Drum Rack.
8. On the MIDI track, set `MIDI From` to the vocal audio track / Sattari VoxKey plugin output.
9. Set the MIDI track Monitor to `In`, or arm it with Auto monitoring.
10. Arm the audio track to record the real vocal.
11. Arm the MIDI track to record generated MIDI.
12. Sing into the mic. Expected result: dry vocal remains audible/recordable and the instrument plays the notes you sing.

If Live does not expose the AU as a MIDI source, use the VST3 build first.

## Microphone test

1. Start in `Melody` mode.
2. Scale: `Chromatic`.
3. Sensitivity: ~62%.
4. Smoothness: ~55%.
5. Confidence: ~58%.
6. Hold: 40–80 ms. Release: 120–180 ms.
7. Sing sustained “ah” notes.
8. If it misses notes, raise Sensitivity or lower Confidence.
9. If it jitters, raise Smoothness/Hold.
10. If notes hang, click MIDI Panic.

## Improvements after MVP

- Manual Ableton mic routing proof: dry vocal audio clip + generated MIDI clip.
- Tune detector parameters against real vocal takes.
- Improve Drum Mouth with onset, brightness, and noise classification instead of pitch-zone MVP mapping.
- Add chord voicing controls for Harmony: close/open, sevenths, sus, gospel/soul colors.
- Add preset modes: Vocal Lead, Bass Spirit, Gospel Harmony, Soul Pad, Mouth Drums.
- Add latency/CPU benchmark and selectable detector window size.
- Add MPE/expression mode.
- Build signed/notarized installer when it clears manual DAW validation.
