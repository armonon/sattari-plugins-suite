# Build + Validation Notes

Minimum source/build gate before pushing major plugin changes:

```bash
./scripts/validate_sources.sh
cmake -S . -B build/all -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/all --config Release -j 4
```

## Rubber Band time/pitch engine

Rubber Band is now built **from vendored source** (no Homebrew, no pkg-config) via
`cmake/SattariRubberBand.cmake`, which compiles `third_party/rubberband/single/RubberBandSingle.cpp`
into a static `sattari_rubberband` target and links it into the products that need it
(StemDeck, Auto Pitch, and Royal Chain — whose Tune module shares the same engine). It is **on by default**; configure with `-DSATTARI_USE_RUBBERBAND=OFF`
for a portable fallback build. On Apple the engine uses vDSP and links the Accelerate framework
automatically.

The source is vendored at `third_party/rubberband` (v3.3.0). To track it as a proper git
submodule instead, replace that directory on your machine:

```bash
rm -rf third_party/rubberband third_party/.rubberband_broken
git submodule add https://github.com/breakfastquay/rubberband.git third_party/rubberband
git -C third_party/rubberband checkout v3.3.0
```

**License:** Rubber Band is **GPLv2-or-commercial** (`third_party/rubberband/COPYING`).
**Decision (locked in): the whole suite ships under GPLv3** — see [`LICENSING.md`](LICENSING.md)
and [`LICENSE`](LICENSE). JUCE (all 8 plugins) and Rubber Band (Auto Pitch, StemDeck, Royal Chain)
are both used under their open-source terms, and GPLv3 satisfies both. Selling binaries is still
allowed; you must offer the corresponding source under GPLv3 to anyone you distribute to. A future
closed-source/paid-exclusive release would instead require commercial JUCE + Rubber Band licences
(or swapping Rubber Band for a permissive engine like Signalsmith Stretch).

Any reusable plugin can be machine-validated through the shared gate:

```bash
./scripts/validate_plugin.sh <artefacts-Release-dir> [AU_TYPE AU_SUBTYPE [AU_MANUFACTURER]]
```

Machine validation gates for each built AU/VST3, where applicable:

```bash
codesign --verify --deep --strict --verbose=2 "path/to/Plugin.component"
codesign --verify --deep --strict --verbose=2 "path/to/Plugin.vst3"
auval -v <type> <subtype> Satr
/Applications/pluginval.app/Contents/MacOS/pluginval --validate-in-process --strictness-level 5 "path/to/Plugin.vst3"
```

Do not call any plugin sale-ready until manual DAW listening/install/product gates pass.

StemDeck-specific bounded gate/package helpers:

```bash
./scripts/validate_stemdeck.sh
./scripts/package_stemdeck_alpha.sh
```

The StemDeck package helper emits a truth-labeled alpha validation package; it is not an installer and does not replace DAW/listening validation.

Manual DAW/listening reports can be started with:

```bash
./scripts/stemdeck_manual_validation_template.sh
```

StemDeck's current plugin-hosting work is only a JUCE format-manager discovery/load scaffold; hosted plugin audio/MIDI routing still needs implementation and manual validation.
