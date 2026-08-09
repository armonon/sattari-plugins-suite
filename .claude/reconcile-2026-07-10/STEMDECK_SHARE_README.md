# Sattari StemDeck

**Status:** standalone Mac DJ app alpha / internal validation build  
**Formats:** Standalone macOS app, VST3, AU build target present  
**Current version:** 0.4.3

Sattari StemDeck is a four-deck DJ and remix workspace for performing with full tracks, stems, effects, pads, and live recording. It is built as an app-first JUCE/C++ product with plugin builds available for DAW validation.

StemDeck is currently an alpha. It is useful for internal testing, product demos, engineering validation, and early workflow feedback. It is not yet a notarized or sale-ready release.

## What It Does

- Loads audio onto four independent decks: Deck A, B, C, and D.
- Supports full-mix playback plus stem lanes for Drums, Bass, Music, and Vocals.
- Provides deck controls for play, cue, gain, fader, pitch, EQ, filter, key lock, live key analysis, and tempo/grid sync.
- Includes host tempo sync, beat sync, beat-grid offset, downbeat offset, and tempo interpretation controls.
- Uses Rubber Band when linked for pitch-preserving time/pitch moves, with an internal granular fallback route.
- Offers an offline AI stem workflow through a configurable local separator command, such as Demucs.
- Caches generated stems locally and automatically switches to stem-lane playback when all core stems are ready.
- Includes a mic/input monitor path.
- Includes eight sample pads with load, trigger, and gain controls.
- Hosts a plugin-chain scaffold for deck, mic, and master FX experiments.
- Records the performed master mix, mic, per-deck renders, event log, and arrangement-style JSON metadata.

## Quick Start

1. Open the Standalone app.
2. Load or drag a full mix onto Deck A, B, C, or D.
3. Press Play and mix with deck faders, EQ, filters, crossfader, and master controls.
4. Optional: configure local Demucs, then use `AI Split` to generate Drums/Bass/Music/Vocals stems.
5. Optional: trigger one-shot samples from the pad section.
6. Press `Record WAV` to capture the performance and arrangement metadata.

## Optional AI Stem Setup

StemDeck does not currently bundle an AI stem model. For local alpha testing, use the included Demucs helper:

```bash
./scripts/setup_stemdeck_demucs.sh
```

That creates a local ignored virtual environment and writes StemDeck's separator command configuration. After setup, loading a track and pressing `AI Split` can generate cached stem files for that source.

## Build

From the repository root:

```bash
cmake -S plugins/stemdeck -B build/stemdeck -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/stemdeck --config Release --target SattariStemDeck_Standalone SattariStemDeck_VST3 -j 4
```

## Validation

Run the StemDeck local gate:

```bash
./scripts/validate_stemdeck.sh
```

The gate builds the app/plugin targets and runs deterministic contracts for:

- granular/key-lock rendering
- Rubber Band time/pitch benchmark output
- beat-sync and warp planning
- offline stem cache boundaries
- separator command wiring
- arrangement/event-log JSON
- HostGraph empty-chain pass-through
- codesign verification
- pluginval strictness 5 when pluginval is available

The latest local scan passed the full gate on July 10, 2026.

## Current Alpha Limits

- Not notarized yet.
- Not sale-ready.
- Real-song listening validation is still required.
- Long-session soak testing is still required.
- DAW matrix validation is still required across Ableton Live, Logic Pro, FL Studio, and Reaper.
- The AI stem workflow depends on a local separator setup and is not yet a bundled consumer installer flow.
- Plugin hosting is scaffolded, but chain management, hosted editor UX, state persistence, and broad third-party compatibility still need product work.
- Audio files are loaded into memory; long-set streaming/caching is future work.
- Beat-grid tools are functional but not yet a full DJ-style editable beatgrid editor.
- The arrangement recorder writes useful metadata, but it is not yet a full DAW timeline editor.

## Intended Use

StemDeck is currently best used for:

- internal alpha demos
- engineering validation
- workflow testing
- DJ/remix concept review
- listening tests with known licensed audio
- feedback on the app-first performance layout

Please do not present this build as a finished commercial product until the sale-readiness gates, listening tests, installer/notarization, and product packaging are complete.

