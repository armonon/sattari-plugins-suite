---
type: operating-standard
category: sattari
area: audio-plugins
status: active
created: 2026-05-13
updated: 2026-05-13
source: Armon direction in Discord #suite after Sub Conjurer AU/VST3 build
---

# Sattari Audio Plugin Build Standard

## Standing instruction

Armon asked on 2026-05-13 to apply the Sub Conjurer AU/VST3 upgrade to **all plugins moving forward**.

For Sattari Audio products, the default target is no longer “prototype folder” or “local renderer only.” The default product path is:

> **JUCE/CMake source → AU + VST3 + Standalone builds → installed local plugin → codesign + auval + pluginval → DAW/manual listening validation → packaged deliverable.**

This standard applies to Auto Pitch, Pure Tone Generator Pro, Sub Conjurer, Vocal Doubler + Harmonizer, Resonance Hunter, Rhythm Genie, Cymatic Generator, and any future Sattari plugin unless Armon explicitly chooses a non-plugin product format.

## Toolchain baseline

Local baseline established during the Sub Conjurer v0.1 plugin build:

- CMake via Homebrew
- Ninja via Homebrew
- JUCE cloned under `third_party/JUCE/`
- VST3 SDK cloned under `third_party/vst3sdk/`
- pluginval app installed at `/Applications/pluginval.app`
- Apple Command Line Tools + clang
- `auval` and `codesign`

Full Xcode may still be required later for installer/notarization/App Store-grade workflows, but the first internal AU/VST3 build path proved viable with the current baseline.

## Required project structure

Each plugin should get a source tree under:

```text
plugins/<plugin-slug>/
  CMakeLists.txt
  src/PluginProcessor.h
  src/PluginProcessor.cpp
  src/PluginEditor.h
  src/PluginEditor.cpp
```

Build outputs should stay under:

```text
build/<plugin-slug>/
```

Packaged internal test builds should live under:

```text
deliverables/sattari-test-builds/<plugin-slug>-plugin-vX.Y/
deliverables/sattari-test-builds/<plugin-slug>-plugin-vX.Y.tar.gz
```

## Default formats

Default formats for Sattari plugins:

- AU for Logic/GarageBand
- VST3 for Ableton/FL Studio and broader DAW support
- Standalone app when useful for testing or non-DAW users

Exceptions:

- MIDI-only products such as Rhythm Genie may be `IS_MIDI_EFFECT TRUE` or a MIDI generator plugin/standalone hybrid.
- Generator products such as Pure Tone may be `IS_SYNTH TRUE` or standalone+instrument depending on UX.
- Offline/editor products can keep a renderer, but renderer-only is no longer the final product target unless Armon explicitly asks.

## Minimum validation gates

Every internal plugin build must run and document:

```bash
cmake -S plugins/<plugin-slug> -B build/<plugin-slug> -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/<plugin-slug> --config Release --target <Targets> -j 4
codesign --verify --deep --strict --verbose=2 <AU path>
codesign --verify --deep --strict --verbose=2 <VST3 path>
auval -v <type> <subtype> <manufacturer>
/Applications/pluginval.app/Contents/MacOS/pluginval --validate-in-process --strictness-level 5 <VST3 path>
```

Also required before any sale-ready claim:

- Manual DAW smoke in Logic for AU.
- Manual DAW smoke in Ableton or FL Studio for VST3.
- Real owned/cleared input material.
- Listening notes: usefulness, artifacts, wrong notes, latency, automation, crashes, CPU, preset behavior.
- Repeatable install instructions.

## Packaging contract

Each packaged internal build must include:

- Built AU/VST3/Standalone artifacts.
- Source snapshot or source path reference.
- `README.md` with truth label and current limitations.
- `BUILD.md` with exact build commands.
- `scripts/install_user_plugins.sh` or equivalent.
- `scripts/validate_local.sh` or equivalent.
- Validation results in a dated wiki research/build note.

## Truth labels

Use these labels precisely:

- **internal plugin build** — AU/VST3 exists and passes machine validation.
- **DAW-validated candidate** — Logic/Ableton/FL manual smoke passed on real material.
- **sale-candidate** — product UX/docs/install path are coherent and real-user validation is strong.
- **sale-ready** — meets `sale-ready-quality-bar.md` completely.

Do **not** call a plugin sale-ready just because AU/VST3 builds.

## Current proof

The first proof of this standard is Sub Conjurer:

- Source: `plugins/sub-conjurer/`
- AU/VST3/Standalone build: `build/sub-conjurer/SubConjurer_artefacts/Release/`
- Installed AU: `~/Library/Audio/Plug-Ins/Components/Sub Conjurer.component`
- Installed VST3: `~/Library/Audio/Plug-Ins/VST3/Sub Conjurer.vst3`
- Package: `deliverables/sattari-test-builds/sub-conjurer-plugin-v0.1.tar.gz`
- Build note: `wiki/projects/sattari/research/2026-05-13-sub-conjurer-au-vst3-first-build.md`

Validation passed for Sub Conjurer:

- codesign verify for AU/VST3
- `auval -v aumf Sc01 Satr`
- pluginval strictness 5 for VST3

## Product-specific application

- **Auto Pitch:** convert proven offline DSP and Pitch Compass ideas into AU/VST3 insert effect after real-vocal DSP quality gate. Use plugin shell as soon as core correction quality is credible.
- **Pure Tone Generator Pro:** target standalone + AU/VST3 instrument/generator. Existing renderer becomes golden reference for plugin output validation.
- **Sub Conjurer:** continue from existing AU/VST3 v0.1 and prioritize DAW listening/DSP tuning.
- **Vocal Doubler + Harmonizer:** reuse Auto Pitch confidence/segmentation primitives, then build as AU/VST3 insert effect.
- **Resonance Hunter:** AU/VST3 insert effect with learn/analyze mode and visual feedback.
- **Rhythm Genie:** MIDI effect/generator plugin + standalone; must support DAW MIDI routing and drag/export workflow.
- **Cymatic Generator:** standalone + plugin visualizer; plugin format still required unless Armon chooses standalone-only.
