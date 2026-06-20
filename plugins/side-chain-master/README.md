# Side Chain Master

Truth label: **internal plugin build**.

Side Chain Master is a Sattari JUCE sidechain ducking plugin. It builds as AU, VST3, and Standalone.

## Build

```bash
cmake -S plugins/side-chain-master -B build/side-chain-master -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/side-chain-master --config Release -j 4
```

## Validate

```bash
codesign --verify --deep --strict --verbose=2 "build/side-chain-master/SideChainMaster_artefacts/Release/AU/Side Chain Master.component"
codesign --verify --deep --strict --verbose=2 "build/side-chain-master/SideChainMaster_artefacts/Release/VST3/Side Chain Master.vst3"
auval -v aufx Scm1 Satr
/Applications/pluginval.app/Contents/MacOS/pluginval --validate-in-process --strictness-level 5 "build/side-chain-master/SideChainMaster_artefacts/Release/VST3/Side Chain Master.vst3"
```

## Current limitations

- Needs Logic/Ableton/FL manual sidechain routing validation.
- No GR history display or lookahead yet.
- Not sale-ready.
