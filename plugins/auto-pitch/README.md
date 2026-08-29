# Sattari Auto Pitch

Flagship automatic tuning plugin for AU, VST3, and Standalone on macOS.

## Current product

Auto Pitch has two intentionally distinct audio paths:

- **Monophonic vocal correction:** YIN detection by default, optional conservative
  YIN/CREPE fusion, per-note scale targeting, vibrato/flex controls, formant control,
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

- **Studio HQ:** Rubber Band R3/Finer with its short vocal window, the
  quality-locked monophonic path. R3 is explicitly selected; the former build
  silently used the default R2 engine. The short window materially improves
  fast correction timing while retaining R3's non-percussive vocal quality.

The 0.5.0 RC3 path reports 2,885 samples at 48 kHz (60.10 ms). The delayed-dry
path nulls at negative infinity with zero alignment error; forced-unity R3 and
a genuinely corrected onset land 0.77 ms either side of the centred host report.
Mean steady-note correction error is 0.01 cents. A measured -20 ms moving-control
offset on Classic mode suppresses a controlled 5.5 Hz vibrato from 19.7 to 5.1
cents without changing steady correction or fresh note transitions. R3/Finer is
still a print-quality engine: begin testing at 48 kHz / 512 samples; 64/128-sample
and 96 kHz sessions remain outside the approved envelope.

Adaptive 128-sample ratio interpolation keeps note-transition timing within
5.3 ms across 64–1024-sample host buffers without taxing stationary notes. The
experimental legacy PSOLA path remains serialized for old-session compatibility
but is not selectable. `SattariLivePitchEngine` is connected to the visible Live
Mode: causal YIN, waveform-locked epochs, exact unity dry and transient-safe
crossfades. Two explicit profiles avoid hiding a frequency-range tradeoff:

- **Ultra C3+:** 571 samples / 11.90 ms reported and measured at 48 kHz,
  0.08-cent mean / 0.17-cent max across 28 detune cases, optimized for
  fundamentals at C3 and above.
- **Extended Range:** 1,037 samples / 21.60 ms at 48 kHz, 0.08-cent mean /
  0.19-cent max across 40 cases from E2 through C5, with a 70 Hz detector floor.

Both profiles measure 0.69% corrected-tone THD and pass the qualified 44.1/48 kHz
64-1024 buffer CPU matrix. Exact graph-note targets, per-object
retune/transition/vibrato, bypass, generated vibrato, and Humanize reach both
paths. AutoKey is clocked by analysis hops rather than host callbacks, so key
confidence does not change with buffer size. Neural Pitch's guarded fused estimate
now guides the causal planner while the waveform tracker remains responsible for
phase-coherent epoch placement; it passes the qualified 512-sample CPU point.
Real-vocal listening and target-DAW stress remain required before a commercial
quality claim.

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

Auto Pitch and Sattari Key now use the same Aarden/chroma `KeyAnalysisEngine`,
the same release confidence threshold, and the same **No Result** semantics.
The optional neural key models are deliberately quarantined: `keynet_spec`
scored 37.5% exact on its only held-out evaluation versus 85.4% for Aarden.
Low-confidence AutoKey never drives correction; the visible manual key remains
in force. This quarantine does not affect Neural Pitch, whose embedded CREPE
model is fused conservatively with YIN and remains opt-in because it costs more
CPU.

For a complete internal tester build, run
`scripts/package_auto_pitch_neural_tester.sh`. It includes the embedded pitch
model's MIT notice and provenance while keeping optional key models external.

## Validation commands

```bash
cmake --build build/auto-pitch-neural --target AutoPitchCoreContract AutoPitchRender AutoPitchBench AutoPitchObjectiveBench
./build/auto-pitch-neural/AutoPitchCoreContract
./build/auto-pitch-neural/AutoPitchCoreContract --render-ui-edit /tmp/auto-pitch-edit.png
./build/auto-pitch-neural/AutoPitchRender --smoke
./build/auto-pitch-neural/AutoPitchRender --hitracktest
./build/auto-pitch-neural/AutoPitchObjectiveBench
```

Release builds pin `SATTARI_AUTOPITCH_PRINT_ENGINE=RUBBERBAND`. This is the
engine covered by the latency, null, golden-render, objective, and listening
evidence. Initialising the optional Signalsmith submodule no longer changes the
audio path. Signalsmith can be selected explicitly for engineering evaluation,
but it must pass the complete qualification set before it can replace the
release engine.

## Truth label

This is ready for structured internal testing, not a sale-ready release. It now
has the core workflows expected of a modern automatic tuner, but equivalence to
Auto-Tune Pro is not claimed: qualified ARA host delivery, AAX/Pro Tools delivery,
advanced curve/line tools, MusicXML/score integration, multi-track editing, and a
licensed proprietary-quality neural corpus remain external or future work. The
release gates are real-vocal listening across voice types, dense polyphonic
stress material, multi-instance/automation sessions in target DAWs, signed and
notarized distribution, and a licensed model delivery decision.
