# Sattari Audio Plugins

Internal JUCE/CMake plugin source for the current Sattari Audio plugin line.

## Included plugins and suite foundation

- `core/` — header-only Sattari Audio Core scaffold for shared release gates, render manifests, preset/MIDI/export contracts, and validation report data shapes.
- `plugins/sub-conjurer` — AU/VST3/Standalone low-end/sub helper internal build.
- `plugins/stemdeck` — VST3/Standalone dual-deck remix instrument/internal build.
- `plugins/side-chain-master` — AU/VST3/Standalone side-chain utility internal build.
- `plugins/royal-chain` — AU/VST3/Standalone vocal-chain concept/internal build.
- `validation/` — suite-level schemas/templates for render manifests, Auto Pitch vocal corpus manifests, product validation, and listening/visual reports.

## Truth label

These are **internal plugin builds**, not sale-ready releases. Do not market them as final products until DAW/manual listening, install, UX, and commercial packaging gates pass.

## License

The whole suite is licensed under **GPLv3** — see [`LICENSE`](LICENSE) and
[`LICENSING.md`](LICENSING.md). (JUCE and Rubber Band are both used under their
open-source terms; GPLv3 satisfies both.) You may sell binaries, but must offer the
corresponding source under GPLv3 to anyone you distribute to.

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
