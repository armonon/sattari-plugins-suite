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

- **Basic:** choose the sound source, effective key/mode, correction feel, and
  Studio/Live engine. The large key control always shows whether Manual,
  AutoKey, Key Map, or Song Key is authoritative.
- **Pro:** tune detector/engine behaviour, reference pitch, MIDI targets,
  formants, Flex-Tune, humanize, throat, vibrato, polyphonic scale lock, and the
  modulation-aware Key Map scanner.
- **Edit:** arm Capture, play the song, then drag captured note objects vertically
  to retarget them or right-click to bypass individual corrections. Edits persist
  in the host session and are applied by transport time.

Factory starting points now cover natural correction, hard/vintage tuning,
modern rap, robot, low-latency tracking, MIDI-directed tuning, graph editing,
and generated natural vibrato.

See [`docs/auto-pitch-parity.md`](../../docs/auto-pitch-parity.md) for the
feature-by-feature implementation and remaining external SDK gates.

## Engines and measured baseline

- **Studio:** Rubber Band, highest-fidelity print path.
- **Live:** PSOLA monitoring path.
- **Low Latency:** shortest PSOLA monitoring path.

The current 48 kHz synthetic benchmark measured approximately 41.9 ms Studio,
29.1 ms Live, and 15.9 ms Low Latency, with mean steady-note correction error
of 0.01 cents for Rubber Band and 0.07 cents for PSOLA. These are controlled
engineering measurements, not a claim about every singer or session.

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

Models are loaded from `~/Library/Sattari Audio/models/`:

- `crepe.onnx` — optional monophonic neural pitch
- `keynet.onnx` — optional live neural key classification
- `keynet_spec.onnx` — optional 30-second spectral key classification and Key Map

The model binaries are not distributed by this repository. Record their exact
training/export provenance and redistribution rights before including them in a
tester or commercial package. Without them the plugin remains functional and
visibly falls back to YIN plus deterministic key analysis.

For a complete internal tester build, run
`scripts/package_auto_pitch_neural_tester.sh`. It requires all three models to be
present for build-machine validation, fingerprints them, and deliberately keeps
them out of the package. Model distribution is separately gated by
`scripts/verify_auto_pitch_models.py --distribution --record <record.json>`.

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
Auto-Tune Pro is not claimed: ARA-style offline capture, AAX/Pro Tools delivery,
advanced curve/line tools, note split/join, MusicXML/score integration, and a
licensed proprietary-quality neural corpus remain external or future work. The
release gates are real-vocal listening across voice types, dense polyphonic
stress material, multi-instance/automation sessions in target DAWs, signed and
notarized distribution, and a licensed model delivery decision.
