You're picking up the Sattari Audio plugin suite (JUCE/CMake; repo root = this folder) after a
batch of SOURCE-level changes that were made in another tool and have NOT been compiled. Treat
this like a PR that needs to go green: build everything, fix any compile errors introduced by
these changes, validate, and report back.

## First, setup
- `git submodule update --init --recursive`   (JUCE lives at third_party/JUCE)
- `rm -rf third_party/.rubberband_broken`      (leftover stub from a failed clone; safe to delete)
- Rubber Band source is vendored at `third_party/rubberband` (v3.3.0), built from source via
  `cmake/SattariRubberBand.cmake`. It is NOT a git submodule yet — optionally convert with
  `git submodule add https://github.com/breakfastquay/rubberband.git third_party/rubberband`.

## What changed this session (build + verify each)
1. **Packaging.** `cmake/SattariPlugin.cmake` `sattari_add_plugin()` now links `juce::juce_dsp`
   suite-wide. New `cmake/SattariDeploymentTarget.cmake` sets `CMAKE_OSX_DEPLOYMENT_TARGET 10.13`
   **before `project()`** and is `include()`d in the root CMakeLists + all 8 plugin CMakeLists.
   Verify the floor stamped correctly: `vtool -show-build` on a built `.vst3` binary (expect
   minos 10.13, not your build OS).
2. **All 8 plugins** now build through `sattari_add_plugin` (sub-conjurer, bus, side-chain-master,
   royal-chain, sattari-arp, voxkey, auto-pitch, stemdeck). auto-pitch + stemdeck keep their own
   test targets and `sattari_enable_rubberband()` calls; stemdeck keeps its `JUCE_PLUGINHOST_*`
   defs (added after the helper call).
3. **UI overhaul.** `core/include/sattari_audio_core/SattariLookAndFeel.h` was rewritten to an
   ultra-modern minimal look (flat dark, single hairline frame, thin accent top line, thin-ring
   rotaries with a small indicator dot, flat combos/buttons, switch-style toggles); palette in
   `SattariBranding.h` updated. All 8 editors inherit it — public method names are unchanged, so
   no per-editor edits were needed.
4. **bus drive anti-aliasing.** `plugins/bus` now 4x-oversamples ONLY the drive nonlinearity via
   `juce::dsp::Oversampling`, with the parallel-mix dry latency-compensated and
   `setLatencySamples()` reporting the oversampler latency. `processBlock` is split into
   pre-drive (trim/EQ/comp) → oversampled shaper → post-drive (width/pan/mix/ceiling).

## Build + validate
- `cmake -S . -B build/all -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/all -j`
- Per plugin: `scripts/validate_plugin.sh <artefacts-Release-dir> [AU_TYPE AU_SUBTYPE Satr]`
  (codesign + auval + pluginval@5).
- `python3 scripts/check_brand_registry_sync.py` (must pass: 8 products, registry == Hub manifest)
- `./scripts/validate_sources.sh` and `python3 scripts/suite_gate_check.py`

## Watch-items (written without a compiler — check these first)
- **bus** `PluginProcessor.cpp`: confirm the `juce::dsp::Oversampling`/`AudioBlock` usage compiles;
  the oversampler is created for `jlimit(1,2,outputChannels)` in `prepareToPlay` and the
  processBlock builds the `AudioBlock` from `buffer` — confirm channel counts match (there's a
  base-rate fallback if not). Confirm `getLatencySamples()` is nonzero and DAW delay-comp lines
  up. A/B the new oversampled drive vs the old inline tanh on a hot signal (expect markedly less
  aliasing at high drive, no level jump). Note: drive at 0 already applies a ~1.31x tanh colour —
  pre-existing; leave it unless you want drive=0 to be unity.
- **SattariLookAndFeel.h**: build one editor, screenshot, confirm the modern look renders (thin
  knobs, switch toggles, flat combos, accent top line). `drawToggleButton` now draws a switch +
  label — confirm it fits existing toggle bounds in each editor (some set explicit widths); widen
  if a label clips. Tune palette/sizes in `SattariBranding.h` (kBackground / kHairline /
  kTextMuted / kTitleSize …) to taste.
- **Deployment-target include**: each plugin CMakeLists has
  `include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)` before
  `project()`; the root uses the `cmake/` path. Confirm standalone per-plugin configures resolve it.

## Hard rules (do not break)
- New/edited plugins go through `sattari_add_plugin()`; never hand-roll `juce_add_plugin`.
- Adding a plugin = update root CMake + `SattariBranding.h` registry + Hub
  `sattari-products.json` (ids/names/accents must match or `check_brand_registry_sync.py` fails).
- No allocation/locks/IO on the audio thread; publish meter/analysis to the UI via
  `sattari::rt::AtomicSnapshot`.
- Do NOT replace `SattariLookAndFeel` per plugin — the unified family look is intentional.
- Truth labels: nothing is "sale-ready" until rebuilt + auval/pluginval'd AND DAW-listened.

## Report back
Build result per plugin, any compile fixes you made, pluginval/auval status, a screenshot of one
editor in the new look, and your A/B observation on the bus drive.
