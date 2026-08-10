# Sattari Audio — Agent Handoff Brief

Paste this into another chat that's working on a Sattari plugin. It explains the platform,
the rules that keep all products consistent, exactly how to add or change a plugin, the
current state, and the StemDeck phase-1 plan to execute. Repo root references are relative to
`sattari-plugins/`.

---

## What this repo is

A JUCE/CMake **platform**, not seven loose plugins. There is one shared core, one packaging
path, one look + preset system, one Hub installer, and CI. Eight products today:
**auto-pitch, voxkey, stemdeck, sub-conjurer, side-chain-master, royal-chain, sattari-arp, bus.**

Build the whole suite: `cmake -S . -B build/all -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/all -j4`.
One plugin: `cmake -S plugins/<slug> -B build/<slug> -G Ninja -DCMAKE_BUILD_TYPE=Release`.
JUCE is a submodule: `git submodule update --init --recursive` first.

## Hard rules (do not break these)

1. **Packaging goes through `sattari_add_plugin()`** (`cmake/SattariPlugin.cmake`). Never
   hand-roll `juce_add_plugin` in a plugin's CMakeLists. A plugin CMakeLists is:
   ```cmake
   cmake_minimum_required(VERSION 3.22)
   project(MY_PLUGIN VERSION 0.1.0)
   set(CMAKE_CXX_STANDARD 17)
   set(CMAKE_CXX_STANDARD_REQUIRED ON)
   include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
   sattari_add_plugin(
       TARGET MyPlugin
       SLUG my-plugin
       PRODUCT_NAME "My Plugin"
       PLUGIN_CODE Mp01          # unique 4-char code
       # option flags as needed: IS_SYNTH NEEDS_MIDI_INPUT NEEDS_MIDI_OUTPUT
       #                         IS_MIDI_EFFECT EDITOR_WANTS_KEYBOARD_FOCUS
       # FORMATS AU VST3 Standalone   (this is the default; override only to narrow)
       # VERSION ${PROJECT_VERSION}   (optional)
       SOURCES src/PluginProcessor.cpp src/PluginEditor.cpp)
   ```
   Extra compile defs (e.g. plugin-host flags) and test executables go *after* the call.

2. **Editors use the shared UI + presets.** Include from `<sattari_audio_core/...>`:
   `SattariBranding.h`, `SattariLookAndFeel.h`, `SattariPresetManager.h`. Pattern:
   construct `SattariLookAndFeel lookAndFeel { sattari::brand::productAccent("<slug>") };`,
   `setLookAndFeel(&lookAndFeel)` in the ctor and `setLookAndFeel(nullptr)` in the (real)
   destructor, paint via `lookAndFeel.paintSuiteBackground(g, getLocalBounds())`, and add a
   `SattariPresetManager presetManager { processor.parameters(), "<slug>" };` with a Save
   button + a "User presets" combo. Copy any wired plugin (e.g. `plugins/bus` or
   `plugins/sub-conjurer`) as the template.

3. **Realtime safety on the audio thread.** No allocation, locking, file I/O, or logging in
   `processBlock`. Preallocate scratch in `prepareToPlay`. Publish meter/analysis values to
   the editor with `sattari::rt::AtomicSnapshot<T>` (in `SattariRealtime.h`), never by reading
   a plain member off the audio thread.

4. **Adding a NEW plugin updates THREE places or CI fails:**
   - `CMakeLists.txt` (root) — `add_subdirectory(plugins/<slug>)`.
   - `core/include/sattari_audio_core/SattariBranding.h` — add `{ "<slug>", "Display Name",
     "Category", 0xAARRGGBB }` to the registry.
   - `apps/sattari-hub/Sources/SattariHub/Resources/sattari-products.json` — add a product
     with the **same id, name, and `accent` (#rrggbb)**, plus bundles.
   `scripts/check_brand_registry_sync.py` fails the build if the registry and the Hub manifest
   disagree on ids/names/accents. Run it after any change here.

5. **Time/pitch uses the vendored Rubber Band**, never Homebrew. In CMake:
   `include(.../cmake/SattariRubberBand.cmake)` then
   `sattari_enable_rubberband(MyTarget MY_HAS_RUBBERBAND_DEFINE)`. Guard the code with
   `#if MY_HAS_RUBBERBAND_DEFINE`. **License: Rubber Band is GPLv2-or-commercial — a
   closed-source paid release needs the commercial licence.**

6. **Truth labels.** A plugin is an "internal build" until rebuilt + `auval`/`pluginval`'d via
   `scripts/validate_plugin.sh <artefacts-Release-dir> [auType auSub Satr]` AND DAW-listened on
   real material. Don't write "sale-ready" before that.

## Current state (8 plugins)

- **sub-conjurer** — sub-bass enhancer. DSP upgraded (low-freq tracking ring, SR-correct
  limiter). Reference adopter of the shared core + packaging.
- **side-chain-master** — sidechain compressor. DSP clean.
- **voxkey** — voice→MIDI. Real YIN detector; functional contract test.
- **auto-pitch** — autotune. RubberBand engine; audio-thread allocs fixed; keeps trace import.
- **royal-chain** — modular vocal chain.
- **sattari-arp** — MIDI arpeggiator. Per-block/step allocations fixed.
- **stemdeck** — 4-deck DJ/stem performance + arrangement app. The flagship;
  architecture is broad and functional, while external DSP/hardware/DAW evidence
  remains the largest validation program in the suite.
- **bus** — new bus/aux channel strip (glue comp, tone, drive, width, pan, mix, ceiling).

All on the shared LookAndFeel + preset system + `sattari_add_plugin()`. Nothing in this repo is
DAW-listening-validated yet.

## StemDeck current release program (execute on Macs with ears and real hardware)

A DJ tool lives or dies on sync and key/time-stretch quality. The original phase-1
engine work is implemented; do not rebuild it from this old brief. Close the remaining
gates in this order on **known-BPM / known-key licensed material**:

1. **Beatgrid/key corpus certification.** Spectral-flux onset analysis,
   DBN/Viterbi-style beat decoding, meter/downbeat inference, neural confidence
   gating, variable-tempo warp markers, and manual corrections exist. Add licensed
   ambient, breakdown, live-drummer, modulation, and tempo-change tracks to the
   manifest gate; finish a visual per-marker correction editor.
2. **Long sync + controller validation.** The master-clock phase path and synthetic
   contracts exist. Prove drift over multi-hour sessions while exercising physical
   MIDI controllers, audio devices, cue output, unplug/replug, and sample-rate changes.
3. **Live key validation.** DSP + optional neural key detection exist. Measure false
   positives and confidence behavior through breakdowns/modulations, and ensure a
   weak estimate cannot create an audible auto-match jump.
4. **Listenable time-stretch.** Rubber Band is wired (`SATTARI_STEMDECK_HAS_RUBBERBAND`). Tune
   options (engine/finer, transients, channels-together) and A/B against the resample
   reference using `build/.../timepitch-benchmark/`. **Acceptance:** no bass wobble / transient
   smear at ±8% tempo on real songs.
5. **Productize what is already built.** Choose commercially cleared neural weights,
   isolate hosted plugins out of process, validate captured plugin/master replay,
   run AU/VST3 DAW matrices, and execute the signed/notarized clean-machine package.
   The capture bridge and Build mode are functional alpha systems, not greenfield work.

Discipline: **don't try to out-djay djay/Serato/rekordbox.** StemDeck wins only on the
bridge (perform → keep → build → release). Everything else just needs to be good enough.

## File map

- `cmake/SattariPlugin.cmake` — `sattari_add_plugin()`.
- `cmake/SattariRubberBand.cmake` — `sattari_enable_rubberband()`; engine at `third_party/rubberband`.
- `core/include/sattari_audio_core/` — `SattariBranding.h`, `SattariLookAndFeel.h`,
  `SattariPresetManager.h`, `SattariRealtime.h`, `SattariAudioCore.h`.
- `scripts/` — `validate_plugin.sh`, `check_brand_registry_sync.py`, `validate_sources.sh`,
  `suite_gate_check.py`.
- `apps/sattari-hub/` — installer; manifest at `Sources/SattariHub/Resources/sattari-products.json`.
- `docs/sattari-platform-roadmap.md` — the program strategy.

## Gotchas

- This was authored in a headless Linux sandbox: source/structure/math are verified, but
  **nothing is compiled** — first `cmake` + `auval`/`pluginval` on macOS is the real gate.
- Delete the failed-clone stub: `rm -rf third_party/.rubberband_broken`.
- `bus` drive has no oversampling yet — aliases at high settings; add 2–4x oversampling before
  marketing it.
