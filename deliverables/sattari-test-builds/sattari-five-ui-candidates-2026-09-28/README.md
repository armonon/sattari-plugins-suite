# Sattari UI Candidate Builds

Internal macOS Universal builds: Arp, Sub Conjurer, Vocal and Create 0.1.1;
Mix 0.2.1. All five include AU and VST3. The four audio effects also include
Standalone applications. Arp is a MIDI effect and has no standalone build.

## Install

Close your DAW, then run `bash install_user_plugins.sh` from this directory.
This replaces only these five products in your user plugin/application folders.
Restart or rescan the DAW. These builds are ad-hoc signed, not notarized public
installers. Do not disable system-wide security settings to load them.

## Interface

Arp and Sub Conjurer have dedicated performance surfaces. Mix opens to Session
balance with its existing automixing workflow, with Signal chain on the second
tab. Vocal and Create open directly to their racks. Select a numbered slot to
inspect its controls; the arrows exchange adjacent modules and their bypass
states. Existing sessions, parameter IDs and audio processing are retained.

The visual report and native screenshots are in `validation/README.md`.
`validation/results.json` and logs record the checks on these builds.
`SHA256SUMS` covers the bundles and included working-tree source archive.
Lucide notices are included alongside the repository's licensing documents.

## Build

Extract the source snapshot, then configure the desired product with CMake:

```sh
cmake -S plugins/mix -B build/mix -G Ninja -DCMAKE_BUILD_TYPE=Release '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'
cmake --build build/mix --target SattariMix_AU SattariMix_VST3 SattariMix_Standalone SattariMixUIContract
build/mix/SattariMixUIContract
```

The same pattern applies to the other product folders/targets listed in the
validation script. The local build reused the existing JUCE build helper; its
CMake entry point is included for provenance and contains build-machine paths.

## Release Status

Manual DAW validation, real-material listening, competitive evaluation,
Developer ID signing/notarization, clean-machine installer testing and license
review remain required. Machine checks are not a claim of sale readiness or
industry-leading audio quality. Do not publish this package as a final release.
