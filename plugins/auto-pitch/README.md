# Sattari Auto Pitch

Flagship realtime tuning plugin for AU, VST3, and Standalone on macOS.

## Current product

Auto Pitch has two intentionally distinct audio paths:

- **Monophonic vocal correction:** YIN detection by default, optional CREPE neural
  detection, per-note scale targeting, vibrato/flex controls, formant control,
  editable pitch classes, MIDI note targeting, reference tuning, generated
  vibrato, and Natural, Classic, Modern Rap, Robot, and Big Harmony styles.
- **Polyphonic tonal correction:** a phase-vocoder path for sustained instruments
  and well-separated chords, with High Tracking, Tonal Focus, Scale Lock,
  polyphonic harmony, and whole-source Transpose to Key.

The musical target can come from a manual key (major/minor/chromatic, church
modes, harmonic/melodic minor, pentatonics, or blues), held MIDI notes, live
AutoKey, the cross-plugin Song Key bus, an offline Key Map that follows
modulations across a track, or captured note objects in the Edit timeline.
The UI reports which source and detector are actually active rather than hiding
fallback behavior.

## Three-part workflow

- **Basic:** choose the sound source, effective key/mode, and correction feel.
  The quality-locked Studio engine avoids the experimental PSOLA path. The
  large key control always shows whether Manual,
  AutoKey, Key Map, or Song Key is authoritative.
- **Pro:** tune detector/engine behaviour, reference pitch, MIDI targets,
  formants, Flex-Tune, humanize, throat, vibrato, polyphonic scale lock, and the
  modulation-aware Key Map scanner.
- **Edit:** arm Capture, play the song, then drag captured note objects vertically
  to retarget them or right-click to bypass individual corrections. Edits persist
  in the host session and are applied by transport time.

Factory starting points cover natural correction, hard/vintage tuning, modern
rap, robot, MIDI-directed tuning, graph editing, and generated natural vibrato.

See [`docs/auto-pitch-parity.md`](../../docs/auto-pitch-parity.md) for the
feature-by-feature implementation and remaining external SDK gates.

## Engines and measured baseline

- **Studio:** Rubber Band, the quality-locked monophonic path.

The current 48 kHz synthetic benchmark measured approximately 43.3 ms group
delay against 42.7 ms reported latency and 0.01 cents mean steady-note error.
Adaptive 128-sample ratio interpolation keeps note-transition timing within
5.3 ms across 64–1024-sample host buffers without taxing stationary notes. The
experimental PSOLA path remains serialized for old-session compatibility but
is not selectable until it passes real-vocal quality testing.

## Build and prove the neural release shape

```bash
./scripts/build_auto_pitch_neural.sh
```

That lane downloads Microsoft's pinned universal ONNX Runtime 1.22.0, verifies
its SHA-256, requires neural code to compile, builds universal Release bundles,
embeds the runtime, removes absolute build-machine rpaths, verifies signatures,
and runs the core plus audio-render smoke tests. The official runtime requires
macOS 13.3, so this neural lane uses that honest deployment floor without
raising the rest of the suite.

The MIT CREPE-tiny pitch model is embedded in every neural build. Its pinned
source, hashes, license, release record, and deterministic converter live in
`plugins/auto-pitch/models/` and `scripts/fetch_crepe_onnx.py`.

Optional key models are still loaded from `~/Library/Sattari Audio/models/`:

- `keynet.onnx` — optional live neural key classification
- `keynet_spec.onnx` — optional 30-second spectral key classification and Key Map

Neural Pitch is opt-in because the CNN costs materially more CPU than YIN. If
the key models are absent, AutoKey and Key Map visibly fall back to deterministic
analysis; pitch detection still has both YIN and the bundled neural option.

For a complete internal tester build, run
`scripts/package_auto_pitch_neural_tester.sh`. It includes the embedded pitch
model's MIT notice and provenance while keeping optional key models external.

## Validation commands

```bash
cmake --build build/auto-pitch-neural --target AutoPitchCoreContract AutoPitchRender AutoPitchBench
./build/auto-pitch-neural/AutoPitchCoreContract
./build/auto-pitch-neural/AutoPitchCoreContract --render-ui-edit /tmp/auto-pitch-edit.png
./build/auto-pitch-neural/AutoPitchRender --smoke
./build/auto-pitch-neural/AutoPitchRender --hitracktest
./build/auto-pitch-neural/AutoPitchBench
```

## Truth label

This is ready for structured internal testing, not a sale-ready release. It now
has the core workflows expected of a modern automatic tuner, but equivalence to
Auto-Tune Pro is not claimed: qualified ARA host delivery, AAX/Pro Tools delivery,
advanced curve/line tools, MusicXML/score integration, multi-track editing, and a
licensed proprietary-quality neural corpus remain external or future work. The
release gates are real-vocal listening across voice types, dense polyphonic
stress material, multi-instance/automation sessions in target DAWs, signed and
notarized distribution, and a licensed model delivery decision.
