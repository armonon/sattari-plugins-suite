# Sattari Modular Development Upgrade

Native JUCE development build. The source has advanced beyond the packaged
0.1.1 candidate; its existing qualification record does not certify these changes.
Public release approval is still pending. See RELEASE.md and the exact package's
manifest and validation logs.
It reuses the suite's Clean, EQ, Comp, Heat, Echo, Space, Pulse, De-Ess, Image,
Double, Riser, Shimmer, Warble, Warp, Vocoder, 808, Granulator, and Rhythm Genie
DSP: 22 effects and three source engines, plus development VST3 instrument/effect
hosting. It does not yet contain every Sattari product as a native module.
Version 0.2.0 adds Gate, Face, Glass Choir, Transient, Multiband and Peak effects,
Chop as a source, four layers, and Vocal/Create/Instruments/Mix collection filters.

## Signal path

- FX: mono/stereo input through six serial effect slots.
- Instrument, or FX in Play: four MIDI source layers replace the input, then
  feed the same serial rack. Granulator includes a playable harmonic source;
  Rhythm Genie responds to GM drum notes (kick 36, snare 38, hi-hat 42).
- Rhythm Genie follows host playback, or free-runs its pattern when no host
  timeline is available. Density and Style regenerate a deterministic pattern.
- Two additional serial slots form a post-rack parallel return. The Return
  knob controls their summed contribution. Empty/bypassed return lanes mute.
  Factory Echo and Space use wet-only settings; other modules can pass dry
  signal, so adding them to the return can increase level.
- Enable **8 serial** to place all eight effects in one serial chain instead.
  Return level is disabled in this mode; effects can move across the old boundary.
- Master output, power, and stereo RMS/peak meters. 0 VU is -18 dBFS.
- Native latency compensation remains active for empty and bypassed slots.
  Hosted plugin latency adds to the graph and is reported to the parent host.
- Duplicate modules have separate DSP histories and controls. A duplicate
  captures the current macro-driven setting without sharing its assignment.
- Effects retain their quick controls and expose additional native parameters
  in an expandable detailed view, with capacity for 64 controls. Granulator and
  Rhythm Genie also expose their additional supported source parameters.
- Host and on-screen MIDI reach each effect independently, including Vocoder's
  carrier and 808's note override. Consuming MIDI in one effect does not starve
  later effects or the return lane.
- Each source layer has level, pan, MIDI key range, transpose and velocity curve.
  Level and pan changes ramp over 20 ms. Mapping changes release held notes.
  Key/transpose/velocity mapping applies to incoming notes, not Rhythm Genie's
  internally generated pattern. Four layers do not imply full Stack editor parity.
- Chop analyzes the layer's imported sample on the message thread and maps onset
  slices from MIDI note 36 upwards. Sensitivity changes reslice asynchronously.

All eight macros have two assignable targets with reversible normalized ranges.
Use each macro's settings icon to choose a target and its endpoints. Assigned
controls display the effective value and are edited through their macro.
Assignments follow module reordering. DAW automation remains slot-based:
replacing/reordering modules changes what a slot's automation controls.
Macro destinations retain the original eight quick controls. Detailed controls
are directly host-automatable but are not additional macro destinations yet.

Scenes 1-3 and A/B store rack parameters and assignments, not separate samples.
Each scene retains edits when switching away. Undo/redo covers rack edits,
scene changes, A/B changes, and sample ownership when moving/duplicating layers.
Sample imports and whole-preset file loads start a new undo session. Source
samples are embedded in plugin
state and `.sattarimodular` files, up to 1,440,000 frames per layer (30 seconds
at 48 kHz). The optional Safety clip bounds the processed output to +/-0.98;
it is a hard sample clip, not a transparent limiter or true-peak guarantee.
It defaults off, including when loading legacy state. Master/host bypass still
passes the latency-aligned original without this processing. Keep monitoring
levels low: drive, feedback, return gain and stacked modules can clip internally.

Fourteen factory programs retain the original seven indices and append vocal,
four-layer instrument, Chop and bus chains. These are starting
chains, not listening-approved commercial preset banks.

## Legacy collections and import

Vocal and Create are now collections inside Modular. The legacy wrappers remain
buildable with unchanged plugin IDs for existing sessions. Modular FX can import
their schema-1 JSON `.sattaripreset` files using the import button. Eight slots,
actual module values and bypass settings are copied into an eight-serial chain.
Duplicate module types which the old rack skipped remain bypassed on import.
Import is one undoable operation; malformed, oversized, out-of-range and actively
modulated legacy presets fail before mutation. Active legacy LFOs are not migrated.

This is not a DAW session or automation converter, and does not promise an exact
audio null against the old rack's different latency/gain path. Stack preset files,
dual-source granular editing, sample zones, capture/MPE and the full native rhythm
editor remain in the legacy instrument. Harmony and Maqam are not being deleted
or relabeled as equivalent engines. See the current consolidation record in
`docs/suite-consolidation-2026-10-02.md`.

## External instruments and effects

Choose **External Instrument** in a source layer (Play view), or **External
Effect** in a serial/return slot. The folder button scans one selected `.vst3`
file or bundle; if it contains multiple compatible plugins, a menu selects one.
The adjacent editor button opens that plugin's own controls, with a generic
parameter editor fallback. Both Modular variants support this; FX source layers
run in Play mode. Sattari instruments/effects can also be loaded as VST3s, except
Modular itself, which is rejected to prevent direct recursion.

- Instruments receive the layer's mapped MIDI and level/pan settings. Effects
  receive independent copies of incoming MIDI. Generated plugin MIDI is not
  forwarded to subsequent slots or the DAW. SysEx and external sidechains are
  not supported. Return effects should normally use their own wet-only setting.
- Instances, opaque plugin state and file/class descriptions follow duplication,
  moves, undo, scenes, A/B, host sessions and `.sattarimodular` files. Missing
  plugins keep their saved state and a visible placeholder. Select the matching
  VST3 through the folder button to relink it. Plugin binaries, licenses and
  external sample libraries are not embedded or transferred between computers.
- Mono/stereo main buses are supported. Additional buses are disabled; plugins
  that require them are rejected. Stereo-only plugins can run in a mono rack
  through input duplication/output averaging. Source layers and the return's
  dry path are aligned to hosted latency, including bypass. The maximum accepted
  latency is 16,384 samples per plugin. Changes to latency reset delay histories.
- Only VST3 hosting is enabled. AU, VST2, AAX, multi-output routing, third-party
  parameter automation/macro mapping and a persistent whole-library scanner are
  not implemented. Use the hosted editor for its parameters; Modular's existing
  slot bypass and layer controls remain host-automatable.
- Scanning, loading, state restoration and plugin editors are **in-process**.
  A broken plugin can crash the DAW. Only load trusted, installed plugins with
  the same CPU architecture as Modular. Loading runs on the message thread,
  and pending instruments are silent/pending effects pass dry. Audio emits
  silence instead of waiting while a plugin's state/configuration lock is held.
  Loading, saving and switching hosted scenes are not seamless live operations.
- Presets accept up to 32 MiB per hosted state blob and 256 MiB overall, including
  scenes, A/B and native samples. Very large sample-based plugin states need
  separate qualification; use externally referenced libraries where supported.
  Oversized disk saves leave the previous preset file intact.

## Build

From the repository root, with CMake, a C++17 compiler, and pinned JUCE present:

```sh
cmake -S plugins/modular -B build/modular -DCMAKE_BUILD_TYPE=Release -DSATTARI_COPY_PLUGINS_AFTER_BUILD=OFF -DSATTARI_USE_ONNX=OFF
cmake --build build/modular --config Release --target SattariModularBuild --parallel 4
ctest --test-dir build/modular -C Release --output-on-failure
```

On macOS, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` at configure time for
universal AU, VST3, and standalone applications. On Windows use Visual Studio
2022's C++ workload, `-G "Visual Studio 17 2022" -A x64`, and a new build folder.
Windows produces VST3 and standalone EXE targets; AU is Apple-only.
For faster edit/test cycles, add `-DSATTARI_MODULAR_LTO=OFF`. Release compilation
still uses optimization; this disables only the expensive whole-program link.
The manual Modular Platforms workflow builds both platforms without changing
the existing five-product candidate workflow.

## Validation and release limits

The two native contract executables verify independent instances, routing,
macro application, EQ/Heat audio changes, MIDI sources, state and source
embedding, malformed-state rejection, oversized blocks, mono/stereo operation,
and editor rendering at three sizes. Each writes a five-second native audio
demo. Run pluginval on the resulting VST3 bundles and auval on installed AUs.
Two additional hosting contracts build non-installed test VST3 instruments and
effects to exercise real scanning/loading, state, MIDI, audio, duplication,
missing-plugin recovery, editors and latency. These fixtures are not products
and must not be included in distribution archives.

Earlier hosting-upgrade verification passed all four CTest contracts,
eight packaging regression tests, and pluginval strictness 10 for both Modular
VST3s. An installed Vital instrument feeding ValhallaVintageVerb also produced
nonzero finite output before and after state recall. This is a compatibility
smoke test, not listening approval or a general certification of either product.
Evidence is in `build/upgrade-modular/hosting-contracts.xml`,
`hosting-pluginval-fx.log`, `hosting-pluginval-instrument.log` and the
`modular-verification` subdirectories. Mac AU/VST3/Standalone builds were generated
without replacing installed bundles; Windows and Intel execution remain untested.

Current 0.2.0 evidence is recorded separately in the consolidation document;
the earlier logs do not certify this merged version.

This native development build is not a public-release certification. Commercial
signing/notarization, Windows execution, broader DAW sessions, stress/CPU
profiling, crash recovery, accessibility, and listening tests remain release
gates. Module replacement resets DSP histories; it is intended for building a
chain, not click-free performance switching. Full granular sample/zone editors,
full Stack parity, arbitrary routing,
external sidechain, crash-isolated plugin hosting, and automatic mixing are not
implemented in this version.

Glass Choir prepares on selection (or asynchronously after automated selection),
and its latency participates in the rack's compensation. Until a newly selected
engine is ready, effects pass dry and instruments remain silent. The 808 module's
pitch analysis is expensive in local automation sweeps and needs performance
qualification before a release decision.

## Mac installation

Check `manifest.json` for the exact package's signing and notarization status.
AU bundles belong in `~/Library/Audio/Plug-Ins/Components`; VST3 bundles belong
in `~/Library/Audio/Plug-Ins/VST3`. Rescan plugins in the DAW after installation.
Use Modular FX on an audio track and Modular Instrument on a MIDI/instrument
track. The Instrument standalone application can be played without a DAW.
Choose the desired audio/MIDI device in the standalone Settings menu. Avoid
routing a microphone straight to speakers through the FX standalone app.

The stop-circle button sends all-notes-off if a controller leaves a held note.
The upgrade preserves all previous 423 parameter IDs, indices and normalized
ranges, including the original 160. New source layers and detail banks append;
saved state is now version 4, with versions 1-3 migration. New presets are not
backward-readable by earlier binaries. Vocal/Create file import is supported as
described above, but Stack presets and DAW automation are not migrated. Closing the DAW
before replacing installed bundles avoids loading a mixture of old and new code.

The manual **Product Upgrade Builds** workflow builds and tests Modular, Mix and
Royal Chain on macOS universal and Windows x64. Its downloadable artifacts are
development builds without commercial signing, not release-approved installers.
The older **Modular Platforms** qualification workflow remains separate. Both
plugin CMake and qualification packaging now identify the upgrade as 0.2.0;
all release gates must be rerun against an exact package before publishing.

## Qualification

The qualification script rebuilds the exact candidate, runs both native tests
and pluginval at strictness 10, verifies both Mac architectures, and runs auval.
AU validation temporarily replaces only the two Modular components and restores
their previous contents afterward. Close any DAW using Modular first.

```sh
python3 plugins/modular/scripts/qualify.py --build-dir build/modular \
  --output-dir deliverables/sattari-modular \
  --pluginval /Applications/pluginval.app/Contents/MacOS/pluginval --validate-au
```

Add `--sign-identity 'Developer ID Application: ...'` to sign staged Mac bundles
with a configured identity. This does not notarize or publish the package.
Windows uses the same command without `--validate-au`, with a Windows build
folder and the path to `pluginval.exe`. The manual Modular Platforms workflow
downloads a pinned validator, checks its checksum, and performs qualification.
