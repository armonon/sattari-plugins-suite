# Sattari Audio Plugins

Internal JUCE/CMake plugin source for the current Sattari Audio plugin line.

## Current Portfolio

The October 2026 consolidation focuses new product work on **Modular** (FX and
Instrument variants), **Royal Chain**, **Auto Pitch** and **Mix**, with **Tools**
and **Compass** as supporting bundles. Vocal, Create and instrument collections
live inside Modular; legacy wrappers keep their identities for existing sessions.
No product is promoted to public-release approval by this reorganization.
See [the consolidation record](docs/suite-consolidation-2026-10-02.md) for implemented
features, compatibility boundaries, verification and pending migrations. The
inventory below includes legacy, specialist and held products, not only the
current storefront lineup.

The current [launch qualification program](docs/launch-readiness.md) covers all
16 current/specialist binaries with native Mac/Windows builds and exact-artifact
validation. Historical wrapper candidates are not the consolidated launch suite.

## Included plugins and suite foundation

`core/` is the header-only Sattari Audio Core: shared LookAndFeel/branding,
preset + MIDI-learn systems, the cross-plugin session bus (key/tempo/transient),
pitch + key detectors, envelope follower, render harness, and an optional ML
stem-separation seam. Every plugin builds through `cmake/SattariPlugin.cmake`.

Vocal & tuning
- `plugins/auto-pitch` — flagship monophonic correction plus a separate polyphonic tonal path; YIN/optional CREPE detection, Studio and measured 2 ms Extreme Live engines, extended scales, MIDI targeting, reference tuning, editable captured notes, four generated harmony voices, AutoKey, live Dynamic Key follow, and Key Map.
- `plugins/song-key` — full-song, drag/drop, and live key/tempo analysis; a reviewed fixed lock or confidence-gated 1–5 second Dynamic Follow can publish key changes to Auto Pitch and the rest of the suite.
- `plugins/maqam` — microtonal **quarter-tone** tuner for Arabic/Persian/Turkish maqamat.
- `plugins/harmony` — diatonic harmoniser / choir: scale **or chord-track** follow, auto-tune, choir doubler.
- `plugins/royal-chain` — nine-stage vocal chain with the Portal-style Vocal Crown Compass.
- `plugins/vocal-master` — vocal cleanup + master (mid-side isolate, de-noise, de-ess, tone, leveler) with the AI stem-removal seam.
- `plugins/voxkey` — voice-to-MIDI.

Rhythm, low-end & mix
- `plugins/mix` — linked-track studio automixing with editable balance/EQ/dynamics proposals, group audition/undo, and the existing fourteen-module rack. See [`plugins/mix/README.md`](plugins/mix/README.md) for workflow and current limits.
- `plugins/rhythm-genie` — host-synced MIDI drum-pattern generator.
- `plugins/sattari-arp` — MIDI arpeggiator.
- `plugins/sub-conjurer` — low-end / sub generator.
- `plugins/side-chain-master` — side-chain dynamics.
- `plugins/bus` — bus / mix processor.
- `plugins/align` — timing alignment.

DJ / remix
- `plugins/stemdeck` — four-deck remix **instrument** (AU + VST3 + Standalone).

`validation/` holds suite-level schemas/templates. Each plugin ships a pure-C++
contract test, and the tuning/harmony/vocal/rhythm plugins additionally ship an
offline **render proof** (`*Render`) that measures the real audio/MIDI output —
e.g. Maqam snapping to within 0.02 cents of a quarter-tone, Vocal Master dropping
the music ~226 dB while keeping the vocal, Harmony generating the diatonic 5th.

## Truth label

These are **internal plugin builds**, not sale-ready releases. Do not market them as final products until DAW/manual listening, install, UX, and commercial packaging gates pass.

## License

The repository's [`LICENSE`](LICENSE) currently contains GPLv3, but distribution
is pending review: pinned JUCE 8 uses AGPLv3 or commercial terms, not the older
GPL-only arrangement described here previously. No relicensing has been performed.
See [`LICENSING.md`](LICENSING.md) before distributing any beta or paid binaries.

## Dependencies

JUCE is tracked as a Git submodule at `third_party/JUCE`.

```bash
git submodule update --init --recursive
```

## Build

Build one plugin:

```bash
cmake -S plugins/sub-conjurer -B build/sub-conjurer -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/sub-conjurer --config Release -j 4
```

Build the distribution-shaped neural Auto Pitch lane (macOS 13.3+, universal,
pinned ONNX Runtime, embedded dependency, contracts and render smoke):

```bash
./scripts/build_auto_pitch_neural.sh
```

The MIT CREPE-tiny pitch model is reproducibly converted, hash-locked, and
embedded in Auto Pitch. The optional `keynet.onnx` and `keynet_spec.onnx` key
models remain external until their provenance and redistribution rights are
recorded.

Assemble the traceable internal Auto Pitch DAW/listening kit with:

```bash
./scripts/package_auto_pitch_neural_tester.sh
```

It packages verified universal AU/VST3/standalone bundles, the CREPE license and
model provenance, plus fingerprints for any external key models used by the
test candidate.

Or build every plugin from the repository root:

```bash
cmake -S . -B build/all -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/all --config Release -j 4
```

Run the suite structure inventory before claiming a suite gate moved:

```bash
python3 scripts/suite_gate_check.py
```

This only proves required suite scaffolding exists; product quality still requires the product-specific machine/listening/export gates in `validation/templates/`.

See `BUILD.md` and `docs/plugin-build-standard.md` for validation gates.
