# Sattari Modular 0.1.0: Native Mac Alpha

Built and verified locally on September 30, 2026. This is a working native
audio product, not the browser prototype, and not a public-release certification.

## Included

- Modular FX and Modular Instrument, each as AU, VST3, and standalone app.
- All six bundles contain both arm64 and x86_64 executables.
- Real Sattari DSP: nine effects and two instrument sources.
- Analog rack UI, Sattari logo, macros, scenes, A/B, MIDI, sample loading,
  presets, automation, and stereo meters.
- Native UI renders, a five-second native MIDI audio demo, and validation logs.

## Verified

- Both native contract suites passed: 137 individual checks, zero failures.
- Both VST3s passed pluginval strictness level 5 on arm64 and x86_64/Rosetta.
- Both AUs passed Apple's auval on arm64 and x86_64/Rosetta. The first Intel
  cold load was slow but completed; an additional Intel FX run also passed.
- All six bundles passed codesign verification with local ad-hoc signatures.
- Editor rendering was checked at minimum, default, and maximum sizes, with
  control bindings, engineering units, branding, and macro popup lifetime tested.

Intel validation used Rosetta on this Apple Silicon Mac, not a physical Intel
Mac. VST3 validation used pluginval, not the separate Steinberg validator.

The optimized Release build has whole-program LTO disabled. Audio contracts
cover mono/stereo, 44.1/48/96 kHz, oversized blocks, real MIDI output, state and
embedded-sample recall, malformed state, independent duplicates, macro targets,
latency, bypass, reordering, scenes, and undo/redo.

## Installed Locally

The two AU bundles are installed in `~/Library/Audio/Plug-Ins/Components`.
The two VST3 bundles are installed in `~/Library/Audio/Plug-Ins/VST3`.
Existing products were left untouched. Rescan or restart the DAW to discover
Sattari Modular FX and Sattari Modular Instrument. See README.md for usage.

## Still Required Before Public Release

- Windows compilation and execution. A manual cross-platform GitHub workflow
  is included in the repository; no Windows binary is included here.
- Commercial code signing and notarization. These are local ad-hoc builds.
- Real sessions in supported DAWs, physical Intel and Windows coverage,
  extended stress/CPU testing, crash recovery, accessibility, and listening QA.
- Licensing and distribution review for the application and its dependencies.

The rack hosts integrated Sattari modules, not external plugin binaries.
Arbitrary patch routing, external sidechain, deep module editors, and automatic
mixing are not included. There is no output limiter: begin with low monitoring
levels. This build is for local testing, not a claim of industry-best quality.
