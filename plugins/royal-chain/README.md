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
  transitions. It adds a constant 2 ms latency, reported to the host. Formant
  preservation (causal LPC, no extra latency) is on by default in Royal Chain
  (Auto Pitch keeps it off). The engine output is host-block-size invariant and
  locks onto notes after silence in ~14-25 ms. Still alpha: no neural pitch
  guide, and not yet LISTENED against real vocals (measured only). Offline gates:
  `RoyalChainTuneRenderContract`, `SattariExtremeLivePitchContract`.
- Real-vocal measurement tool (local evidence, audio never in git):
  `RoyalChainVocalRender --in <wav> --out <dir>` reports pitch correction on
  actual singing, latency, speed, allocations and Compass dish state.
- Needs DAW/manual listening validation, preset tuning, and product UX polish.
- Not sale-ready.
