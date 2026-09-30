# Sattari Royal Chain

Truth label: **internal plugin build**.

Sattari Royal Chain is a JUCE vocal-chain concept plugin with Clean, Tune, Control, Tone, De-Esser, Color, Width, Space, Finish, Vocal Crown Compass, and macro-control modules. It builds as AU, VST3, and Standalone.

## Build

```bash
cmake -S plugins/royal-chain -B build/royal-chain -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/royal-chain --config Release -j 4
```

## Validate

```bash
codesign --verify --deep --strict --verbose=2 "build/royal-chain/SattariRoyalChain_artefacts/Release/AU/Sattari Royal Chain.component"
codesign --verify --deep --strict --verbose=2 "build/royal-chain/SattariRoyalChain_artefacts/Release/VST3/Sattari Royal Chain.vst3"
auval -v aufx Rc01 Satr
/Applications/pluginval.app/Contents/MacOS/pluginval --validate-in-process --strictness-level 5 "build/royal-chain/SattariRoyalChain_artefacts/Release/VST3/Sattari Royal Chain.vst3"
```

## Current limitations

- The Tune module runs Auto Pitch's Live Mode engine with its default Extreme
  Live (2 ms) profile (`sattari::dsp::ExtremeLivePitchEngine`), key/scale aware,
  with the Vocal Style choosing retune speed, vibrato retention and note
  transitions. It adds a constant 2 ms latency, reported to the host. It is alpha:
  no formant correction (Auto Pitch's Live path has none either), no neural pitch
  guide, and not yet listened against real vocals. Offline gate:
  `RoyalChainTuneRenderContract`.
- Needs real-vocal DAW/manual listening validation, preset tuning, CPU checks, and product UX polish.
- Not sale-ready.
