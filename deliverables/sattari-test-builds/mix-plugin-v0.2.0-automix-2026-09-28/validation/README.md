# Sattari Mix 0.2.0 Validation

Date: 2026-09-28. Status: **internal plugin build**.

## Verified

- Universal AU, VST3 and Standalone binaries contain arm64 and x86_64 slices.
- Strict code-signature verification passes for all three bundles.
- `auval -v aumf Mx01 Satr` passes for installed version 0.2.0.
- VST3 pluginval strictness 5 passes, including state, parameters, editor,
  processing and bus-layout checks. A separate Steinberg validator was not configured.
- Existing Mix product contract passes: 14 modules, 8 factory programs, three
  sample rates (44.1, 48, 96 kHz), and two block sizes (64, 512).
- Shared rack host regression passes using the current shared-header source.
- Auto Mix contract passes natively and as x86_64 under Rosetta, with zero failures.
  This is not testing on physical Intel hardware.
- Native editor snapshots were rendered and visually inspected at 1000x760,
  1180x820, and 1600x1100. See `final-screenshots/`.
- The local installer succeeds, and the installed standalone reports 0.2.0.

## Auto Mix Contract

Coverage includes active-level analysis, silent tails, stereo cancellation,
nonfinite-input rejection, role-based proposals, gain limits, locked tracks,
stale captures, peak budgeting, exact dry and zero-amount audio, gain rendering,
untouched sidechain samples, group discovery, analysis across prepare/restart,
audition/original/keep/undo, command acknowledgement, rollback on a concurrent
lock, changed-rack invalidation, state recall, unique duplicated-instance IDs,
legacy defaults, appended parameter order, cross-process transport, and separate
project-group isolation. Analysis and coordinated updates run without an editor.

The initial test used exact equality for a zero-valued legacy gain. JUCE's range
conversion yields approximately -0.000000805 dB on arm64; the corrected test
allows 0.0001 dB of numeric error and still requires the assistant to be disabled.
The final logs record the actual restored value and successful result.

## Build Provenance

Source: `/Users/lillypad/Projects/sattari-plugins-main`, current working tree.
JUCE revision: `29396c22c93392d6738e021b83196283d6e4d850` (8.0.12).
Compiler: AppleClang 17.0.0.17000603. Configuration: Release, universal macOS.

The build uses `build/mix-workspace/CMakeLists.txt` to reuse the existing JUCE
code-generation executable while compiling the current JUCE source modules and
the normal `plugins/mix` target. Output is in `build/mix-automix/mix/`.
`build.log`, `contract-build.log`, the contract logs, `machine-validation.log`,
and `binary-sha256.txt` record the checks. The regular per-plugin CMake entry
point and installation commands are documented in `plugins/mix/BUILD.md`.

The local AU and VST3 are installed under `~/Library/Audio/Plug-Ins/`; the app is
installed at `~/Applications/Sattari Mix.app`. The package includes the current
product, core, build helpers, JUCE sources, licenses and required logo asset in
`source-snapshot.tar.gz`.

## Remaining Evidence

**LISTENING STATUS: NOT VERIFIED.** Human Logic/Ableton/FL Studio session smoke,
cleared real-multitrack listening, blind competitive preference tests, 64-track
performance qualification, and public-release signing/notarization remain open.
The assistant uses deterministic heuristics and explicit roles. It does not yet
provide learned source recognition, arrangement automation, reference matching,
or autonomous pan/reverb decisions. Level matching and summed peak readouts are
estimates; the assistant is not a true-peak limiter or a mastering loudness meter.
