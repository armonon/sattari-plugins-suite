# Sattari Royal Chain

Truth label: **internal plugin build**.

Sattari Royal Chain is a JUCE vocal-chain concept plugin with Clean, Tune, Control, Tone, De-Esser, Color, Width, Space, Finish, Vocal Crown Compass, and macro-control modules. It builds as AU, VST3, and Standalone.

## Development Upgrade

- One 18-program factory bank now serves both the host and editor. The original
  eight host indices are unchanged; ten formerly editor-only choices append.
  Presets change only their listed values, retaining key/sync choices and any
  omitted settings. Podcast Clean uses the original host definition.
- Nine explicit stage bypasses use 10 ms crossfades. Tune bypass retains its
  reported latency. Clean bypass also bypasses its input trim; Finish bypass
  bypasses its output trim. DSP histories continue to run while bypassed.
- Original auditions the latency-aligned input. Level match uses a smoothed
  RMS estimate to attenuate louder processed audio, by at most 24 dB; it never
  boosts quieter processing. It is not LUFS matching or a limiter.
- New controls append after the original 16 parameters and default off in old
  sessions. Existing macro voicing has not been replaced with shared rack DSP.
- Width modifies existing stereo differences; it does not generate a double
  from identical left/right input. Use Modular's Double for generated doubling.

`RoyalChainProductContract` checks host/editor preset parity, old-state defaults,
bypass/original timing at three sample rates, oversized buffers, RMS matching
and native UI renders. The separate tuning render contract remains required.
The manual Product Upgrade Builds workflow also builds Windows x64 VST3 and
standalone artifacts. It does not certify DAW behavior or signing.

## Build

```bash
cmake -S plugins/royal-chain -B build/royal-chain -G Ninja -DCMAKE_BUILD_TYPE=Release -DSATTARI_COPY_PLUGINS_AFTER_BUILD=OFF
cmake --build build/royal-chain --config Release --target SattariRoyalChain_All RoyalChainProductContract RoyalChainTuneRenderContract -j 4
ctest --test-dir build/royal-chain -C Release --output-on-failure
```

## Validate

```bash
codesign --verify --deep --strict --verbose=2 "build/royal-chain/SattariRoyalChain_artefacts/Release/AU/Sattari Royal Chain.component"
codesign --verify --deep --strict --verbose=2 "build/royal-chain/SattariRoyalChain_artefacts/Release/VST3/Sattari Royal Chain.vst3"
/Applications/pluginval.app/Contents/MacOS/pluginval --validate-in-process --strictness-level 5 "build/royal-chain/SattariRoyalChain_artefacts/Release/VST3/Sattari Royal Chain.vst3"
```

AU validation (`auval -v aufx Rc01 Satr`) requires intentionally installing the
matching AU first; the build above does not replace installed plugins. A result
from an older installed AU is not evidence for this source build.

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
