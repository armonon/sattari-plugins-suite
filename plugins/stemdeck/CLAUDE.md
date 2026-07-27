# Sattari StemDeck — working notes for a focused session

You're working on **StemDeck in isolation**. It's a DJ-style 4-deck remix plugin/app
(VST3 + Standalone, **no AU**), plugin code `Sd01`, ~6.5k LOC, **app-first** direction.
The rest of the Sattari suite is not your concern here unless you touch `core/`.

## You are inside a git worktree

This directory (`~/Downloads/sattari-plugins-stemdeck/`) is a **git worktree** on the
`stemdeck` branch. The sibling `~/Downloads/sattari-plugins-main/` is the same repo on
`main`. The two branches edit independent working trees but share `.git` state, so:

* You can `git commit` here without touching `main`.
* `third_party/JUCE` and `third_party/rubberband` are **symlinks** back to the main
  worktree — you do not need to re-clone them. Build dirs (`build/`) are per-worktree.
* When you're done, `cd ../sattari-plugins-main && git merge stemdeck` to land the work
  on `main` (or open a PR if you push the branch).

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
2. **Plugin-host audio routing** — DONE via `src/HostGraph.{h,cpp}`: per-chain
   `AudioProcessorGraph` (deck/mic/master), async off-thread load, series FX rack,
   RT-safe `processChain` (try-lock, no alloc on the audio thread). Scanning is
   **Sattari-only** by design (third-party VST3s crash the host mid-scan). The old
   `PluginHostManager.{h,cpp}` scaffold was orphaned and has been deleted.
   **Open caveat:** some Sattari guest plugins (Echo, Space) crash in their OWN
   editor's `resized()` when hosted — that's a bug in `sattari-plugins-main`, not here.
3. **Beatgrid / downbeat detection** — IMPLEMENTED in `src/TrackAnalysis.cpp`
   (multiband spectral-flux onset envelope, autocorrelation tempo, Ellis-2007 DP beat
   tracker, variable-tempo curve, bar-phase downbeat/meter). Not yet DAW-validated on
   real songs, and there is no user-editable beatgrid UI.
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
