# Sattari StemDeck — working notes for a focused session

You're working on **StemDeck in isolation**. It's a DJ-style 4-deck remix plugin/app
(VST3 + Standalone, **no AU**), plugin code `Sd01`, ~6.5k LOC, **app-first** direction.
The rest of the Sattari suite is not your concern here unless you touch `core/`.

## Build & validate (per-plugin — fast, no need to build the whole suite)

```bash
# from repo root. Add -DSATTARI_UNIVERSAL=OFF for fast host-arch-only iteration.
cmake -S plugins/stemdeck -B build/stemdeck -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/stemdeck -j

# deterministic contract test (architecture/beat-sync/key-lock/render):
cmake --build build/stemdeck --target StemDeckGranularRenderContract
build/stemdeck/plugins/stemdeck/StemDeckGranularRenderContract   # (path may be build/stemdeck/StemDeck... when configured standalone)

# machine gate (StemDeck has no AU, so VST3 pluginval@5 + codesign only):
scripts/validate_plugin.sh build/stemdeck/SattariStemDeck_artefacts/Release
```

**Standalone codesign quirk:** before validating, re-sign the `.app` with
`codesign --force --deep -s - "<...>/Standalone/Sattari StemDeck.app"` — JUCE's
build-time ad-hoc seal fails `codesign --deep --strict` until re-signed.

## Current state (truth-labeled — keep it honest)

Builds universal (arm64+x86_64), passes **pluginval@5**, `StemDeckGranularRenderContract`
passes. **NOT sale-ready:** no real-song/DAW listening, not notarized. The packaging
plumbing is proven; the hard DSP is not. See
[`docs/stemdeck-sale-readiness-gates.md`](docs/stemdeck-sale-readiness-gates.md).

## The real work (big open items) — with file pointers

1. **Sale-grade time/pitch engine** — `src/TimePitchEngine.cpp`, `src/WarpEngine.cpp`,
   `src/BeatSyncEngine.cpp`. Currently a granular key-lock backend; not DAW-validated.
   Rubber Band is available via `sattari_enable_rubberband()` if you want to lean on it
   (it's already linked — GPLv3, see `/LICENSING.md`).
2. **Plugin-host audio routing** — `src/PluginHostManager.{h,cpp}` deliberately stops at
   discovery + instantiation; the loaded plugin is **not routed** into `DeckPlayer`/
   `processBlock`. RT-safety is the hard part: loads are async on a background thread, so
   processing a swappable instance on the audio thread needs a lock-free swap (don't
   `new`/`delete` or lock on the audio thread). Read the header comment first.
3. **Beatgrid / downbeat detection** — `src/TrackAnalysis.cpp` (lightweight Krumhansl
   chroma key + host-PPQ/BPM beat-offset; no transient/downbeat detection or beatgrid edit).
4. **Notarized installer / onboarding** — `src/MainPluginEditor.cpp` has the standalone
   quick-start rail; packaging via `scripts/package_stemdeck_app_alpha.sh`.

## Shared core you can reuse (don't reinvent these)

- `core/include/sattari_audio_core/SattariPitchDetector.h` — shared monophonic pitch
  detector + MIDI↔Hz helpers (`midiToHz`, `hzToMidi`, `nearestMidiNote`,
  `snapHzToSemitone`, `foldToOctaveNear`) + scale-snap (`ScaleId`, `snapMidiToScale`).
  `TrackAnalysis.cpp` still has its own `midiNoteToFrequency`; replace with `sattari::dsp::midiToHz`.
- `SattariEnvelopeFollower.h` — `process` (rising peak) + `processFalling` (GR smoothing).
- `SattariHostTransport.h` — `sattari::host::read(*this)` returns `{bpm, ppqPosition,
  isPlaying, hasHostBpm, hasPpq}`. The other tempo-sync plugins already adopted it;
  StemDeck's `MainPluginProcessor.cpp` and `TransportSync.cpp` are the two remaining
  sites. **Drop-in:** replace `if (auto* ph = getPlayHead()) ...` blocks with
  `const auto t = sattari::host::read (*this);`.
- `SattariRealtime.h` — `AtomicSnapshot` (lock-free audio→UI publish) + `ScratchBuffer`.
- `SattariLookAndFeel`, `SattariPresetManager`, `SattariBranding`, `SattariAudioCore.h`
  (harmonic engine: `KeySignature`, `HarmonicWheelCode`).

## Hard rules

- Go through `sattari_add_plugin()` (StemDeck already does). Don't hand-roll `juce_add_plugin`.
- **No allocation/locks/IO on the audio thread**; publish meter/analysis to the UI via `AtomicSnapshot`.
- Suite is **GPLv3** (`/LICENSING.md`).
- Nothing is "sale-ready" until rebuilt + pluginval'd **and** DAW-listened.
- **Env:** this folder is not a git repo and `third_party/JUCE` must be a manual clone of
  **8.0.12** (see `/BUILD.md`); `ninja` + `pluginval` are required and not preinstalled.
