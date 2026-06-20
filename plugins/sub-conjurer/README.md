# Sub Conjurer

Truth label: **internal plugin build**.

Sub Conjurer is a Sattari JUCE low-end/sub helper plugin. It builds as AU, VST3, and Standalone.

## Build

```bash
cmake -S plugins/sub-conjurer -B build/sub-conjurer -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/sub-conjurer --config Release -j 4
```

## Validate

```bash
codesign --verify --deep --strict --verbose=2 "build/sub-conjurer/SubConjurer_artefacts/Release/AU/Sub Conjurer.component"
codesign --verify --deep --strict --verbose=2 "build/sub-conjurer/SubConjurer_artefacts/Release/VST3/Sub Conjurer.vst3"
auval -v aumf Sc01 Satr
/Applications/pluginval.app/Contents/MacOS/pluginval --validate-in-process --strictness-level 5 "build/sub-conjurer/SubConjurer_artefacts/Release/VST3/Sub Conjurer.vst3"
```

## Current limitations

- Needs Logic/Ableton/FL manual DAW/listening validation on real kick/bass material.
- DSP tuning, presets, install UX, and docs still need alpha-user feedback.
- Not sale-ready.
