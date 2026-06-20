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
- **stemdeck** — 4-deck DJ/stem app. The flagship. Real engine; **least DSP-validated** (see plan).
- **bus** — new bus/aux channel strip (glue comp, tone, drive, width, pan, mix, ceiling).

All on the shared LookAndFeel + preset system + `sattari_add_plugin()`. Nothing in this repo is
DAW-listening-validated yet.

## StemDeck phase 1 — "make the DJ half real" (execute on a Mac with ears)

A DJ tool lives or dies on sync and key/time-stretch quality. Do these in order; no new
features until Perform mode feels tight. Validate on **known-BPM / known-key** material.

1. **Beatgrid + downbeat detection (the named gap).** Today `BeatSyncEngine` only follows host
   PPQ + per-deck Source BPM + a manual offset — there is no automatic tempo/downbeat
   detection. Build an offline analyzer, e.g. `plugins/stemdeck/src/BeatGridAnalyzer`:
   - onset function = positive spectral flux (or energy-envelope first difference) of the
     loaded full mix;
   - tempo = autocorrelation / comb-filter peak of the onset function over 70–180 BPM;
   - downbeat/phase = best alignment of a beat pulse train to the onset function;
   - output `BeatGrid { double bpm; double firstBeatSeconds; double confidence; }`.
   - **Acceptance:** a synthetic click train at a known BPM resolves within ±0.5 BPM; add a
     `stemdeck_beatgrid_contract.cpp` test (mirror `voxkey_core_contract.cpp`) wired as an
     `EXCLUDE_FROM_ALL` target. Feed it into `BeatSyncEngine` so sync locks to a real grid.
2. **Sync drift.** Drive playback position from a single master sample clock and derive each
   deck's phase from it; don't accumulate per-deck `double` increments. **Acceptance:** two
   decks at the same musical tempo stay phase-locked over a 10-minute render (offline test
   measuring drift in ms).
3. **Live key detection.** Current `TrackAnalysis` is lightweight Krumhansl chroma. Validate
   false positives and behavior during breakdowns/modulations; gate auto-match so a wrong
   estimate can't cause a bad jump. **Acceptance:** correct key on a labeled set of simple +
   complex tracks; no audible mis-match during a set.
4. **Listenable time-stretch.** Rubber Band is wired (`SATTARI_STEMDECK_HAS_RUBBERBAND`). Tune
   options (engine/finer, transients, channels-together) and A/B against the resample
   reference using `build/.../timepitch-benchmark/`. **Acceptance:** no bass wobble / transient
   smear at ±8% tempo on real songs.
5. **Then** zero-setup stem separation (bundle a one-click separator or ship stem packs — the
   "optional Demucs" path is a dealbreaker for normal users), and only after Perform is tight,
   the capture bridge + Build mode.

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
