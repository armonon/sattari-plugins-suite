# 2026-05-25 StemDeck DJ/DAW Architecture Correction

Armon requested a correction to the 4-deck DJ/DAW architecture. The app already has four decks, so this pass kept the four-deck model and added the missing architectural seams instead of adding more decks.

## Implemented

- Added `WarpEngine` as the per-deck timing contract around source BPM, beatgrid, pitch, tempo ratio, sync mode, pitch-preserving decisions, and global master BPM.
- Kept the time-stretch backend abstract via `WarpEngine::TimeStretchBackend` and `BackendDescriptor`.
- Added optional Rubber Band discovery/link flags in CMake (`SATTARI_STEMDECK_HAS_RUBBERBAND`) while preserving the existing realtime granular fallback when Rubber Band is not installed.
- Routed deck processing through `WarpEngine::Plan` so beat-sync and pitch/time decisions are centralized before the deck render path.
- Added `PerformanceRecorder` and `ArrangementBuilder` to replace the earlier capture-only arrangement logic.
- Added per-deck post-fader recorders for Deck A, Deck B, Deck C, Deck D, plus mic and master files.
- Performance event logging now reserves event capacity up front and uses a try-lock path so the audio callback does not wait on the recorder event lock.
- Arrangement JSON v2 now includes deck rendered audio, mic audio, master audio, deck lanes, clips, cue/loop automation lanes, FX/sync/tempo/pitch/EQ/filter/fader automation, and event log linkage.
- Added `HostGraph` with JUCE `AudioProcessorGraph` chains for Deck A-D, mic/vocal, and master.
- Registered plugin formats using current JUCE APIs:
  - `juce::addDefaultFormatsToManager(formatManager)` for Standalone builds.
  - `juce::addHeadlessDefaultFormatsToManager(formatManager)` for plugin/headless builds.
- Added plugin scan cache (`known-plugins.xml`) and dead-man's-pedal crash protection files under the StemDeck HostGraph app-data directory.
- Added `StemProvider` for offline-first cached stems. It inspects/writes a cache manifest in the background and only feeds stem WAVs into deck buffers once cached files exist.

## Validation

- `cmake --build build/stemdeck --target SattariStemDeck -j2` succeeded.
- `cmake --build build/stemdeck --target StemDeckGranularRenderContract -j2 && ./build/stemdeck/StemDeckGranularRenderContract` succeeded.

## Known follow-ups

- Rubber Band is not installed on this machine, so the optional high-quality backend compiled as unavailable and the realtime granular fallback remains active.
- `StemProvider` is cache/offline-first but does not yet bundle a separation model; it loads pre-generated cached WAVs and writes manifests. Next step is wiring an actual offline separator command/model into the provider.
- HostGraph supports chains/scanning/cache/crash protection, but plugin UI/chain management controls still need editor UX.

## Rubber Band install/link verification

- Installed Homebrew `rubberband` 4.0.0 and `pkgconf` so CMake can detect `rubberband.pc`.
- Updated StemDeck CMake to use `pkg_check_modules(... IMPORTED_TARGET rubberband)` and link `PkgConfig::RUBBERBAND` publicly so JUCE Standalone/VST3 format targets inherit the correct library paths.
- Reconfigured `build/stemdeck` and rebuilt:
  - `SattariStemDeck_Standalone` succeeded.
  - `SattariStemDeck_VST3` succeeded and copied to `~/Library/Audio/Plug-Ins/VST3/Sattari StemDeck.vst3`.
  - `StemDeckGranularRenderContract` succeeded.
- Verified both Standalone and VST3 binaries link `/opt/homebrew/opt/rubberband/lib/librubberband.3.dylib` and `libsamplerate` with `otool -L`.

## Critical sprint follow-up: Rubber Band path, FX UI, offline stem command

- Added a real Rubber Band render path inside `DeckPlayer` when `SATTARI_STEMDECK_HAS_RUBBERBAND=1` and key-lock/pitch-preserving stretch is active.
- The deck now prepares a realtime `RubberBand::RubberBandStretcher`, sets time ratio and pitch scale on the audio thread before processing, feeds preallocated deinterleaved buffers, retrieves output into a preallocated buffer, and falls back to the existing granular path if Rubber Band is unavailable/inactive.
- Beat-sync lock changes reset/reposition the Rubber Band stream when the source position jumps materially, avoiding slow drift while not doing disk/network/heap work in the callback.
- Updated time-pitch status labels to show Rubber Band key-lock when compiled with Rubber Band.
- Added HostGraph mutation locking: UI-triggered load/clear/scan cache operations no longer rebuild a graph while the audio callback is processing it; audio uses a try-lock and passes through if a chain is being mutated.
- Exposed HostGraph wrappers through `MainPluginProcessor` for cache load/save, scanning, known plugin list, chain names, load-to-chain, and clear-chain.
- Added a compact plugin-host UI row: chain selector, plugin selector, Scan FX, Reload Cache, Load FX, Clear FX, and status message.
- Added `SATTARI_STEMDECK_SEPARATOR_CMD` support to `StemProvider`. Separation runs in the existing background job, writes to a temp directory, validates `drums.wav`, `bass.wav`, `music.wav`, `vocals.wav`, then atomically moves files into the cache.
- Added optional `scripts/stemdeck_separate_demucs.py` wrapper. It maps Demucs `other.wav` to StemDeck `music.wav` and keeps Demucs optional instead of a build dependency.

Validation after this sprint:

- `cmake --build build/stemdeck --target SattariStemDeck_Standalone -j2` succeeded.
- `cmake --build build/stemdeck --target SattariStemDeck_VST3 -j2` succeeded and copied the VST3 into `~/Library/Audio/Plug-Ins/VST3/Sattari StemDeck.vst3`.
- `cmake --build build/stemdeck --target StemDeckGranularRenderContract -j2 && ./build/stemdeck/StemDeckGranularRenderContract` succeeded.
- `python3 scripts/stemdeck_separate_demucs.py --help` succeeded.
- `otool -L` confirms the VST3 links `/opt/homebrew/opt/rubberband/lib/librubberband.3.dylib`.

Remaining risk / next QA:

- The Rubber Band path is now real and compiled, but still needs hands-on listening/stress QA for latency/startup smoothing, sync jumps, and long-set drift.
- Demucs is not installed locally, so the separator wrapper is wired and help-tested but not yet model-tested end-to-end.

## Demucs install and end-to-end separator smoke

- Created isolated Python 3.12 environment at `.venv/stemdeck-demucs`.
- Homebrew Python needed `DYLD_LIBRARY_PATH=/opt/homebrew/opt/expat/lib` for venv/pip because its `pyexpat` extension expects the Homebrew expat symbols; the launcher script sets this automatically.
- Installed Demucs 4.0.1 plus Torch/Torchaudio and TorchCodec in the venv.
- Added `scripts/stemdeck_separate_demucs_venv.sh`, which launches the Demucs wrapper with the correct venv and expat library path.
- Wrote persistent StemDeck separator config to `~/Library/Application Support/Sattari/StemDeck/StemProvider/separator-command.txt` pointing at the venv launcher.
- Updated `StemProvider` to read that config file when `SATTARI_STEMDECK_SEPARATOR_CMD` is not set, so GUI-launched Standalone/DAW sessions can still find the separator.
- Ran an end-to-end smoke using a generated 2-second WAV. Demucs downloaded the htdemucs model, separated successfully, and produced all four StemDeck cache files:
  - `drums.wav`
  - `bass.wav`
  - `music.wav`
  - `vocals.wav`
- Rebuilt and verified after the install/config change:
  - `SattariStemDeck_Standalone` succeeded.
  - `SattariStemDeck_VST3` succeeded and copied into `~/Library/Audio/Plug-Ins/VST3/Sattari StemDeck.vst3`.
  - `StemDeckGranularRenderContract` succeeded.
