# Sattari Modular 0.1

Native JUCE alpha. This is an actual audio processor, not the browser concept.
It reuses the suite's Clean, EQ, Comp, Heat, Echo, Space, Pulse, De-Ess, Image,
Granulator, and Rhythm Genie DSP. It does not scan or host third-party plugin
binaries, and it does not yet contain every Sattari product.

## Signal path

- FX: mono/stereo input through six serial effect slots.
- Instrument, or FX in Play: two MIDI source layers replace the input, then
  feed the same serial rack. Granulator includes a playable harmonic source;
  Rhythm Genie responds to GM drum notes (kick 36, snare 38, hi-hat 42).
- Rhythm Genie follows host playback, or free-runs its pattern when no host
  timeline is available. Density and Style regenerate a deterministic pattern.
- Two additional serial slots form a post-rack parallel return. The Return
  knob controls their summed contribution. Empty/bypassed return lanes mute.
  Factory Echo and Space use wet-only settings; other modules can pass dry
  signal, so adding them to the return can increase level.
- Master output, power, and stereo RMS/peak meters. 0 VU is -18 dBFS.
- Fixed latency compensation remains active for empty and bypassed slots.
- Duplicate modules have separate DSP histories and controls.

All eight macros have two assignable targets with reversible normalized ranges.
Use each macro's settings icon to choose a target and its endpoints. Assigned
controls display the effective value and are edited through their macro.
Assignments follow module reordering. DAW automation remains slot-based:
replacing/reordering modules changes what a slot's automation controls.

Scenes 1-3 and A/B store rack parameters and assignments, not separate samples.
Each scene retains edits when switching away. Undo/redo covers rack edits,
scene changes, and A/B changes. Sample imports and whole-preset file loads are
session changes, outside rack undo. Source samples are embedded in plugin
state and `.sattarimodular` files, up to 1,440,000 frames per layer (30 seconds
at 48 kHz). Keep monitoring levels low while experimenting: there is no output
limiter, and high drive, feedback, return gain, or stacked modules can clip.

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

This first native alpha is not a public-release certification. Commercial
signing/notarization, Windows execution, broader DAW sessions, stress/CPU
profiling, crash recovery, accessibility, and listening tests remain release
gates. Module replacement resets DSP histories; it is intended for building a
chain, not click-free performance switching. Only the eight selected controls
per module are exposed here; deep module-specific editors, arbitrary routing,
external sidechain, external plugin hosting, and automatic mixing are not
implemented in this version.

## Mac installation

The local alpha bundles use ad-hoc signatures, not commercial notarization.
AU bundles belong in `~/Library/Audio/Plug-Ins/Components`; VST3 bundles belong
in `~/Library/Audio/Plug-Ins/VST3`. Rescan plugins in the DAW after installation.
Use Modular FX on an audio track and Modular Instrument on a MIDI/instrument
track. The Instrument standalone application can be played without a DAW.
Choose the desired audio/MIDI device in the standalone Settings menu. Avoid
routing a microphone straight to speakers through the FX standalone app.
