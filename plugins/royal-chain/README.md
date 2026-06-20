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

- The Tune module now performs real chromatic pitch correction via the suite's
  shared Rubber Band realtime engine (the same engine as Auto Pitch). It is alpha:
  chromatic-only (no key/scale snap yet), not tuned against real vocals, and — like
  Auto Pitch. The suite ships under **GPLv3** (see LICENSING.md), which covers Rubber Band;
  a future closed-source/paid-exclusive release would need a commercial Rubber Band licence.
- Needs real-vocal DAW/manual listening validation, preset tuning, CPU checks, and product UX polish.
- Not sale-ready.
