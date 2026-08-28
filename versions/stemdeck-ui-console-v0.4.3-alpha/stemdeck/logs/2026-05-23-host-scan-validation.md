# StemDeck v0.3.2 host-scan validation log — 2026-05-23

Truth label: **machine validation only / not sale-ready**.

## Implemented slice

- Added `PluginHostManager`, a JUCE `AudioPluginFormatManager`/`KnownPluginList` scaffold for VST3/AU discovery, bounded default-location scanning, and async instance ownership.
- Enabled JUCE host format compilation for VST3/AU in the StemDeck target.
- Surfaced plugin-host scaffold status in the editor; no hosted plugin audio/MIDI routing or plugin UI is connected yet.
- Added/kept v0.3.2 key-analysis and resample time-pitch seam (`TrackAnalysis`, `TimePitchEngine`) with explicit fallback labeling.
- Added `scripts/stemdeck_manual_validation_template.sh` for local DAW/listening evidence capture.
- Updated package/readiness docs and alpha package defaults to `0.3.2-host-scan`.

## Commands run

```bash
./scripts/validate_sources.sh && \
cmake -S plugins/stemdeck -B build/stemdeck -G Ninja -DCMAKE_BUILD_TYPE=Release && \
cmake --build build/stemdeck --config Release --target SattariStemDeck_Standalone SattariStemDeck_VST3 -j 4

./scripts/validate_stemdeck.sh

./scripts/package_stemdeck_alpha.sh
```

## Results

- Source validation: passed.
- CMake configure/build: passed for Standalone and VST3.
- Codesign verification: passed for built VST3 and Standalone app after local ad-hoc signing refresh.
- pluginval: passed strictness level 5 for `build/stemdeck/SattariStemDeck_artefacts/Release/VST3/Sattari StemDeck.vst3`.
- Package created:
  - Directory: `/Users/agentsattari/.openclaw/workspace/deliverables/sattari-test-builds/stemdeck-plugin-alpha-v0.3.2-host-scan`
  - Archive: `/Users/agentsattari/.openclaw/workspace/deliverables/sattari-test-builds/stemdeck-plugin-alpha-v0.3.2-host-scan.tar.gz`

## Explicit blockers remaining

- Manual DAW/listening validation not performed in this repo-only run.
- Plugin-host audio/MIDI routing, hosted editor UX, scan isolation UX, and crash quarantine behavior still need product work.
- Time/pitch path remains resampling fallback, not DJ-grade pitch-preserving time-stretch.
- No AI stem separation, installer/notarization, sale/legal packaging, or product-page evidence yet.
