# Sattari Audio Core

Truth label: **foundation scaffold**. This is the shared suite layer for future Sattari Audio tools. It is not a product and does not make any sale-ready claim.

## Purpose

Stop treating Auto Pitch, Rhythm Genie, Pure Tone Generator Pro, Cymatic Generator, Sub Conjurer, StemDeck, Side Chain Master, and Royal Chain as isolated experiments. New work should reuse this core layer where practical.

## Core modules

- **Reusable audio engine:** shared render context, deterministic render manifests, offline/realtime separation, safety-limiter hooks, metering contracts, and future DSP utility modules.
- **MIDI engine:** shared note/pattern descriptors, General MIDI drum mapping, DAW mapping presets, groove/humanize/swing metadata, and MIDI export contracts.
- **Preset system:** stable preset descriptors, categories, schema versions, migration notes, and product-owned parameter maps.
- **Plugin shell:** common product metadata, AU/VST3/Standalone format flags, validation expectations, and build/report conventions.
- **Standalone wrapper:** shared packaging expectations for non-DAW utility modes.
- **Export system:** WAV/MIDI/PNG/MP4/JSON exports must create render manifests with product version, source commit, inputs, outputs, seeds, and metrics.
- **UI component library:** shared Sattari controls, preset browser, output meter, emergency mute, validation badges, and export panels. Implementation will start as product-agnostic component specs, then migrate to JUCE/web components as wrappers settle.
- **Validation harness:** one suite-level structure for machine tests, export tests, pluginval/auval, listening tests, and known limitations.
- **Listening-test reports:** every gate move requires human listening or visual review where relevant, not just synthetic tests.
- **Harmonic analysis + library prep:** shared contracts for key/BPM/energy/cue/tag metadata, a Sattari-owned harmonic wheel code, and transition-compatibility labels. This gives Auto Pitch, VoxKey, StemDeck, and future standalone suite utilities a common foundation for DJ/producer track analysis without copying competitor branding, UI, code, or proprietary scoring.

## Current code surface

`include/sattari_audio_core/SattariAudioCore.h` defines the first shared C++ data contracts:

- `ReleaseGate`
- `AudioRenderContext`
- `ExportRequest`
- `RenderManifest`
- `PresetDescriptor`
- `MidiPatternDescriptor`
- `ListeningTestItem`
- `ValidationMetric`
- `ProductValidationReport`
- `PluginShellConfig`
- `KeySignature`, `HarmonicWheelCode`, `TrackAnalysis`, `EnergySegment`, `CuePoint`

It also exposes small helper functions for harmonic workflow contracts:

- `toSattariWheelCode(KeySignature)`
- `classifyHarmonicMove(KeySignature, KeySignature)`
- `energyLevel1to10(double)`

## Validate harmonic contracts

```bash
cmake -S core -B build/core-harmonic -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/core-harmonic --target SattariAudioCoreHarmonicContract
./build/core-harmonic/SattariAudioCoreHarmonicContract
```

The initial target is header-only so existing plugin builds are not disrupted. Products should link or include this gradually.
