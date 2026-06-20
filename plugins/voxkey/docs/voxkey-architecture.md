# VoxKey technical architecture

Truth label: MVP architecture for implementation/testing; not final product spec.

## Build path decision

Use JUCE VST3/AU first. Max for Live can still be used for Ableton-only experiments, but the Sattari product path needs a reusable native engine, plugin validation, packaging, and eventual installer/notarization. JUCE also keeps pitch detection, MIDI mapping, and UI reusable across formats.

## Modules

### 1. Audio input / dry pass-through

`PluginProcessor` receives mono/stereo vocal audio and does not clear or replace the main audio buffer. The real vocal remains audible/recordable.

### 2. Pitch detection

`VoxKeyPitchDetector` performs a lightweight YIN-style monophonic pitch estimate over a rolling analysis window. It publishes:

- voiced/unvoiced state
- frequency Hz
- confidence
- RMS

Controls:

- Sensitivity adjusts the input gate and YIN tolerance.
- Confidence threshold blocks breath/noise false notes.
- Smoothness damps detected frequency changes.

### 3. MIDI mapping

`VoxKeyVoiceMidiMapper` converts pitch frames to MIDI events.

Responsibilities:

- Hz → MIDI note
- scale quantization
- octave shift
- RMS → velocity
- hold/release note stabilization
- mode-specific note generation
- panic/all-notes-off state

Modes:

- Melody: one sung note → one MIDI note.
- Bass Follow: sung note → two octaves lower.
- Harmony: sung note → scale-aware triad.
- Drum Mouth: pitch-zone MVP to kick/snare/hat notes.
- Pad/Soul Layer: lower sustained dyad for pad support.

### 4. DAW routing

The plugin is an audio effect with MIDI output enabled:

- `IS_SYNTH FALSE`
- `NEEDS_MIDI_OUTPUT TRUE`
- `IS_MIDI_EFFECT FALSE`

Ableton routes generated MIDI from the audio track hosting VoxKey into a separate MIDI/instrument track.

### 5. UI

`PluginEditor` presents a performance-first surface:

- input/midi orb
- detected note
- MIDI output note
- mode, key, scale
- sensitivity, smoothness, confidence
- octave, hold, release, velocity
- pitch-bend/glide/bypass/panic

## MVP task list

Done in v0.1.0:

- Create plugin target.
- Implement YIN pitch detector.
- Implement voice-to-MIDI mapper.
- Implement audio pass-through + MIDI output.
- Implement Ableton-focused UI.
- Add core contract test.
- Add validation/package scripts.
- Add Ableton setup docs.

Next:

1. Real Ableton microphone smoke test.
2. Confirm MIDI From routing with VST3 and AU in Ableton Live.
3. Add pitch-bend output for vibrato.
4. Improve Drum Mouth with onset/brightness/noise classification.
5. Add legato glide without note reattack where target instruments support it.
6. Add CPU/latency benchmark and tune YIN window sizes.
7. Add scale/chord preset branding: Soul, Gospel, R&B, Cinematic, Bassline, Drum Mouth.
8. Add product installer/notarization after manual DAW confidence.
