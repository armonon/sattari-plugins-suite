# Sattari StemDeck v0.4.3 standalone app alpha

Truth label: **standalone Mac DJ app alpha / internal pre-alpha engine** — JUCE/CMake Standalone + VST3 builds exist, but the product direction is app-first. The local gate builds the Standalone app/VST3 and runs deterministic contracts for architecture, offline stem command wiring, beat-sync, live-key, arrangement export, plugin-host pass-through, key-lock rendering, and a synthetic time/pitch benchmark listening pack. Not sale-ready yet: needs real-song/DAW listening validation, long-session soak testing, notarized installer/onboarding, plugin-host UX/state polish, and product-grade stem progress/cancel UX.

## Concept

Sattari StemDeck is a DJ-style four-deck remix plugin/app groundwork for producers and live DJ use. The current internal build focuses on being opened as its own macOS app, loading tracks, mixing them with deck controls, triggering one-shot pads, recording the performed mix into arrangement-style lanes, and preserving enough architecture for later stem separation, sync, sampler, recording, mic, and plugin-hosting work.

## Standalone quick-start flow

The app now surfaces the app-first workflow directly in the header so an alpha tester can begin without reading this file first:

1. Load or drag a full mix onto Deck A/B/C/D.
2. Use `AI Split` only after local Demucs setup has been configured.
3. Mix with faders, deck FX, plugin-host FX, and performance pads.
4. Hit `Record WAV` to capture the performed arrangement.
5. Treat the build as not notarized and not sale-ready until the open gates below pass.

## Implemented MVP

- JUCE C++ standalone app/plugin engine using `AudioProcessorValueTreeState`.
- Header-level standalone alpha quick-start rail with load → optional AI split → mix/FX/pads → Record WAV flow and not-notarized/not-sale-ready boundary.
- Formats: Standalone app + VST3.
- Four decks: Deck A, Deck B, Deck C, and Deck D.
- External stem lanes per deck: Full mix, Drums, Bass, Music, and Vocals lanes share one deck transport/playhead.
- Optional offline AI stem separation command seam via `SATTARI_STEMDECK_SEPARATOR_CMD` or `~/Library/Application Support/Sattari/StemDeck/StemProvider/separator-command.txt`.
- Local Demucs setup helper: `scripts/setup_stemdeck_demucs.sh` creates `.venv/stemdeck-demucs`, installs Demucs/TorchCodec, and writes StemDeck's separator config to `scripts/stemdeck_separate_demucs_venv.sh`.
- Premium AI stem deck UX: each deck has an `AI Split` action plus a cache/status line that reports queued/running/ready states and ready stem count.
- Premium stem mix handoff: when all four core generated stems are cached, StemDeck loads Drums/Bass/Music/Vocals and automatically mutes the Full Mix lane to avoid doubled playback while leaving stem lane gains active.
- Drag-and-drop waveform zones and file chooser loading.
- WAV/AIFF/FLAC support via JUCE basic formats; MP3 is registered when the local JUCE build exposes MP3 support.
- Waveform thumbnails per deck.
- Play toggle and cue/rewind per deck.
- Per-deck gain trim plus per-deck channel fader before the A/C vs B/D crossfader groups.
- 3-band low/mid/high EQ per deck.
- Deck filter knob per deck.
- Equal-power crossfader across deck groups A/C and B/D.
- Master gain and sample-level safety limiter.
- Host tempo snapshot display where available.
- Host tempo sync: each deck has its own editable source BPM parameter and, when Host Sync is on, playback follows host BPM by deck-specific BPM ratio.
- Beat sync/grid lock: a new global Beat Sync toggle can phase-lock decks to the host PPQ beat grid while still using each deck's individual Source BPM. Each deck also has an automatable Beat Grid Offset control for nudging alignment by beats when a track's downbeat/grid needs correction.
- Internal granular key-lock backend: when Preserve is enabled and a tempo/key/pitch move is active, deck playback uses a two-grain Hann overlap engine that advances content tempo separately from local grain pitch. It is a real pitch-preserving internal backend, but still an alpha-quality algorithm that needs listening/DAW validation before sale-candidate claims.
- Resample fallback: when Preserve is off, host sync and key/pitch controls still use the earlier playback-rate resample path.
- Key-analysis and key-match parameters: loaded full-mix audio gets a lightweight chroma/Krumhansl key estimate plus RMS/duration analysis, with manual pitch and target-key controls routed through either granular key-lock or the resample fallback depending on Preserve.
- Live key analysis/match mode: each deck has a `LiveKey` toggle. When enabled, the UI periodically analyzes an 8-second window around the deck playhead and uses that live key estimate for target-key matching and pitch-shift decisions when confidence is available, falling back to the load-time full-track key when live confidence is weak or not ready.
- Mic/input monitoring path with input bus, monitor toggle, and mic/input gain.
- 8 sample pads, each loadable and triggerable as a one-shot from the UI or keyboard keys `1`-`8` when the editor has focus.
- Automatable DAW parameters for deck play, deck source BPM, deck beat-grid offset, deck key preserve/live-key flags, deck target key, deck manual pitch shift, deck gain/fader/EQ/filter, crossfader, master, limiter ceiling, host sync/beat sync flags, mic monitor/gain, and pad gains.
- Automatable DAW parameters for per-deck stem-lane gains.
- Plugin state save/restore for APVTS params plus deck/pad file paths.
- Background thread pool for file loading; audio callback does no file IO.
- Master-output WAV recording via `MasterRecorder`: the finished master buffer is copied into a bounded queue and written by a background thread from the Record WAV button.
- Performance arrangement export groundwork writes a truth-labeled arrangement/event-log JSON for recorded sessions so pre-alpha sets can be rebuilt/audited.

## Architecture classes

- `DeckPlayer`
- `WaveformDisplay`
- `MixerSection`
- `EQProcessor`
- `FilterProcessor`
- `SamplePad`
- `PadBank`
- `TransportSync`
- `MainPluginProcessor`
- `MainPluginEditor`
- `MasterRecorder`
- `PluginHostManager` legacy JUCE `AudioPluginFormatManager` scaffold plus current `HostGraph` scan/load scaffold for VST3/AU discovery, chain selection, load/clear, and real-time-safe pass-through fallback when graph mutation is busy
- `TimePitchEngine` granular key-lock decision seam and `TrackAnalysis` groundwork
- `RoadmapAdapters` header with placeholder interfaces for stem removal, mic input, and plugin hosting

## Build

```bash
cmake -S plugins/stemdeck -B build/stemdeck -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/stemdeck --config Release --target SattariStemDeck_Standalone SattariStemDeck_VST3 -j 4
```

One-command local StemDeck gate:

```bash
./scripts/validate_stemdeck.sh
```

The gate also builds and runs `StemDeckGranularRenderContract`, a deterministic synthetic-audio contract that checks the Preserve-on granular route renders finite non-silent audio, keeps tempo-change pitch below the Preserve-off resample fallback, and raises zero-crossing rate for an octave pitch shift. This is **not** a substitute for real-song/DAW listening validation, but it protects the internal key-lock path from silent or misrouted regressions.

The gate also runs `StemDeckArchitectureContract`, which protects the pre-alpha seams for warp planning, arrangement/event-log JSON, offline stem cache boundaries, configured separator command execution, and empty plugin-chain pass-through.

The gate now also builds and runs `StemDeckTimePitchBenchmark`, which renders a synthetic DJ-loop listening pack under `build/stemdeck/timepitch-benchmark/` with Preserve/key-lock tempo cases, pitch-shift cases, stress cases, a resample reference, and a metrics manifest. This is an internal benchmark/listening aid, not sale-ready evidence by itself.

Optional local Demucs setup/smoke:

```bash
./scripts/setup_stemdeck_demucs.sh
./scripts/stemdeck_separate_demucs_venv.sh --input path/to/song.wav --output-dir tmp/stemdeck-demucs-smoke/out
```

The Demucs venv and smoke outputs are local-only and intentionally ignored by git.

Manual validation report template:

```bash
./scripts/stemdeck_manual_validation_template.sh
```

Truth-labeled standalone app alpha package:

```bash
BUILD_DIR="$PWD/build/stemdeck-app" STEMDECK_APP_VERSION="0.4.3-djay-ui-reference" ./scripts/package_stemdeck_app_alpha.sh
```

Truth-labeled plugin/app alpha package:

```bash
./scripts/package_stemdeck_alpha.sh
```

## Validation

```bash
codesign --verify --deep --strict --verbose=2 "build/stemdeck/SattariStemDeck_artefacts/Release/VST3/Sattari StemDeck.vst3"
codesign --verify --deep --strict --verbose=2 "build/stemdeck/SattariStemDeck_artefacts/Release/Standalone/Sattari StemDeck.app"
/Applications/pluginval.app/Contents/MacOS/pluginval --validate-in-process --strictness-level 5 "build/stemdeck/SattariStemDeck_artefacts/Release/VST3/Sattari StemDeck.vst3"
```

Latest validation log/report: `plugins/stemdeck/logs/2026-05-23-host-scan-validation.md`.

## Current limitations

- Still not finished/sale-ready: DAW tests, real-song listening pass, long-session soak, installer/product packaging, and polished stem/host-plugin UX are still missing.
- Recording write, arrangement JSON export, mic input monitoring, host BPM sync, beat-grid sync, live key analysis/matching, optional offline Demucs stems, and pluginval strictness 5 are internal pre-alpha features, not sale-ready claims.
- Offline AI stems are command-backed and cache-backed, not bundled as a consumer installer. Local Demucs requires `scripts/setup_stemdeck_demucs.sh` or equivalent user setup. The UI now feels like a product flow, but the model/runtime packaging is still developer-local until licensing/notarization are reviewed.
- Stem-lane gain control is automatable; dedicated polished stem-mixer/progress/cancel UX still needs product work.
- Beat-grid sync is host-PPQ/source-BPM based with per-deck beat-offset nudging; it is not yet full DJ-style transient/downbeat detection, beatgrid editing, or commercial-grade time-stretch. Preserve now enables an internal granular key-lock backend, and LiveKey can drive target-key matching from the current playback window, but these have not passed real-song listening validation or DAW matrix testing.
- JUCE plugin-host format discovery/load scaffolding exists for VST3/AU builds, but plugin UI, preset/state UX, chain reorder/bypass/remove polish, and broad third-party plugin compatibility testing are not implemented yet.
- No file-browser library, MIDI pad/controller mapping, installer, or sale-ready packaging yet.
- Keyboard pad triggering requires the editor/Standalone window to have keyboard focus.
- Audio loading copies full files into memory; fine for MVP, but long DJ sets need streaming or smart caching.
- Manual DAW/listening smoke test still required before calling this a DAW-validated candidate.
