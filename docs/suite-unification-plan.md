---
type: operating-standard
category: sattari
area: audio-plugins
status: superseded
created: 2026-06-16
updated: 2026-08-09
source: Suite unification pass — shared core + realtime/DSP upgrades
---

# Sattari Audio Suite — Unification & Upgrade Plan

> Historical plan. The seven-plugin model was superseded on 2026-08-09 by the
> six-product suite in `docs/six-product-suite.md` and `config/products.json`.
> That lineup was itself superseded on 2026-10-02; the current record is
> `docs/suite-consolidation-2026-10-02.md`.
> StemDeck is outside the current suite scope. Keep this file only as the record
> of the shared-core and realtime-safety work that led to the current platform.

Goal: take the seven separate plugins from "each one works and passes `auval`" to **one
cohesive, powerful suite** — a shared identity, a shared preset system, one realtime-safety
standard, and a Hub that ties the whole line together — while raising the DSP quality of the
weaker engines.

This document is the standing reference for that work. It records what shipped in the first
unification pass and the exact, copy-pasteable rollout for the remaining plugins so every one
of the seven lands on the same foundation.

## The shape we are building

Decision (2026-06-16): **unified suite + Hub**, not a single mega-plugin. The seven keep their
own identities and DAW slots, but they stop being seven unrelated codebases and become one
product family:

- a shared **core** (`core/include/sattari_audio_core/`) every plugin links,
- one **look** so all editors are visibly the same brand,
- one **preset system** writing to a shared suite folder the Hub can see,
- one **realtime-safety** standard (no allocations / locks on the audio thread),
- the **Sattari Hub** as the single app to install, update, and open everything.

## Shared core (new in this pass)

Four header-only modules were added under `core/include/sattari_audio_core/`. They are
dependency-light and compile into any plugin that links `SattariAudioCore` (an INTERFACE
target — no extra build cost).

`SattariBranding.h` — the single source of truth for the palette, typography, corner radius,
and the **product registry** (id → display name, category, accent colour). Editors and the Hub
resolve colours/names from here instead of hard-coding hex. Palette is exposed as raw ARGB
constants (usable even without JUCE) with `juce::Colour` helpers when `juce_graphics` is
present.

`SattariLookAndFeel.h` — a `juce::LookAndFeel_V4` subclass giving every editor the same rotary,
slider, combo, button, and label styling plus a shared `paintSuiteBackground()`. Construct one
per editor with the product accent, `setLookAndFeel(&lnf)` in the constructor, and
`setLookAndFeel(nullptr)` in the destructor. Includes static `styleRotary/styleTitle/styleSubtitle`
helpers so editors stay short.

`SattariPresetManager.h` — one preset system for the whole suite, backed by each plugin's
APVTS. Presets are portable JSON (`paramID → real value`, not a raw ValueTree dump) stored at
`~/Library/Application Support/Sattari Audio/Presets/<productId>/<name>.sattaripreset`. Because
they are a flat id→value map, presets survive parameters being added or removed between versions:
unknown ids are ignored, missing ids keep their current value. The Hub (and future cloud sync)
can enumerate every plugin's presets from that one folder.

`SattariRealtime.h` — realtime-safety utilities. `AtomicSnapshot<T>` publishes a small
trivially-copyable struct from the audio thread to the UI with no locks and no data race
(seqlock); it replaces the current pattern of editors reading a plain processor member straight
off the audio thread. `ScratchBuffer` is a `juce::AudioBuffer` sized once in `prepareToPlay` and
reused every block, with a `sumToMono()` helper for the mono-sum several plugins do by hand.

## DSP / realtime fixes applied in this pass

Auto-Pitch (flagship): the fallback pitch-guide and the "Big Harmony" style layer were calling
`AudioBuffer::makeCopyOf` every block — a heap allocation on the audio thread, and Big Harmony
did it even in the RubberBand build. Both now copy into preallocated `formantScratch` /
`harmonyScratch` members sized in `prepareToPlay`. No audio-thread allocations remain on those
paths.

Sattari-Arp: the per-block `MidiBuffer` and the per-step `std::vector` allocations
(`buildStepNotes` / `buildSortedBaseNotes` / `chooseNotesForStep`) were removed. The output
buffer is now a reused member (`scratchOutput`), and the note builders fill reserved member
scratch vectors instead of returning fresh vectors. "As Played" no longer copies the held-note
list (the list is already maintained in play order).

Sub-Conjurer: the pitch follower could only autocorrelate within a single host block, so at a
512-sample block it could not resolve fundamentals below ~94 Hz — exactly its target sub range.
It now feeds a dedicated ~45 ms low-frequency analysis ring and re-estimates on a decimated
schedule (the heavy loop stays gated by the existing RMS check, so silence is cheap). The
limiter recovery is now sample-rate-correct (a ~20 ms time constant) instead of a fixed
per-sample step that drifted with sample rate.

Reference integration: Sub-Conjurer's editor was rewired onto the shared `SattariLookAndFeel`
and `SattariPresetManager` (Save Preset button + a user-preset combo that reads/writes the
shared suite folder), and its `CMakeLists.txt` now links `SattariAudioCore`. This is the proven
template for the other six.

## Per-plugin rollout checklist (the remaining six)

Each plugin gets the same four mechanical steps, then its own short DSP/RT punch list. None of
these require new third-party dependencies.

Mechanical (identical for every plugin):

1. `CMakeLists.txt`: after the JUCE guard add
   ```cmake
   if(NOT TARGET SattariAudioCore)
       add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../core ${CMAKE_BINARY_DIR}/core)
   endif()
   ```
   and add `SattariAudioCore` to the `PRIVATE` link libraries.
2. Editor header: include `SattariBranding.h` + `SattariLookAndFeel.h` (+ `SattariPresetManager.h`
   if the plugin should expose user presets); add a `SattariLookAndFeel lookAndFeel { productAccent("<id>") };`
   member; declare a real destructor.
3. Editor ctor/dtor: `setLookAndFeel(&lookAndFeel)` / `setLookAndFeel(nullptr)`; replace the
   hand-rolled background paint with `lookAndFeel.paintSuiteBackground(g, getLocalBounds())`;
   drop per-widget colour overrides so the shared look drives them.
4. Replace any editor read of an audio-thread member used for metering/analysis with an
   `AtomicSnapshot<T>` published from `processBlock`.

Per-plugin punch list:

- **VoxKey** — DSP is already clean (textbook YIN). Wire shared look + presets. Publish the
  detected pitch/confidence to the editor via `AtomicSnapshot` instead of reading members.
- **Auto-Pitch** — allocations already fixed. Wire shared look + presets (it already has the
  trace-import flow; presets and trace import should live side by side). Move the editor's
  `lastPitchFrame`/status read onto an `AtomicSnapshot`.
- **Side-Chain Master** — DSP clean (correct gain computer). Wire shared look + presets. Its
  gain-reduction meter already uses `std::atomic`; optionally fold it into `AtomicSnapshot` for
  consistency.
- **Royal-Chain** — modular macro chain; wire shared look + presets. Audit each module's
  `processBlock` for per-block allocations and any sample-rate-dependent constants.
- **StemDeck** — biggest engine; DSP is strong (lock-free stem swap, RubberBand + granular
  fallback). Wire shared look + presets across its larger editor. Audit `MainPluginProcessor`
  for any analysis buffers allocated off `prepareToPlay`. Keep the existing truth labels.
- **Sattari-Arp** — allocations already fixed. Wire shared look + presets.

## Hub evolution

The Hub (`apps/sattari-hub`) already installs every product from one app and tracks receipts.
Next steps to make it the real face of the suite:

- Drive the Hub's product list and accents from the **same registry** as the plugins. Either
  generate `sattari-products.json` from `SattariBranding.h::products()` (a small build-time
  script) or keep the JSON authoritative and add a contract test asserting the two agree, so the
  ids/names/accents never drift between plugin and installer.
- Surface the shared preset folder in the Hub: let users see and back up every plugin's presets
  from one place (they already live under `~/Library/Application Support/Sattari Audio/Presets/`).
- Keep the existing truth-label discipline: the Hub stays "internal alpha installer/updater"
  until notarization + a signed update feed exist.

## CI recommendation

The validation today is local shell scripts. Add a GitHub Actions macOS matrix that, per plugin:
configures + builds AU/VST3/Standalone, runs the existing contract tests (they are functional —
e.g. VoxKey asserts a 440 Hz sine resolves to A4), and runs `validate_sources.sh` /
`suite_gate_check.py`. This turns the contract tests into regression gates and catches a broken
plugin the moment the shared core changes — important now that all seven depend on one core.

## Sequencing

1. (done) Build the shared core; fix the three allocation/DSP issues; wire Sub-Conjurer as the
   reference.
2. Roll the four mechanical steps across the remaining six (VoxKey, Auto-Pitch, Side-Chain
   Master, Royal-Chain, StemDeck, Sattari-Arp), compiling and `auval`-checking each on macOS.
3. Unify the Hub against the shared registry; expose the shared preset folder.
4. Stand up the CI matrix.
5. Only then push the per-product sale-readiness gates (real-corpus/DAW listening, notarized
   installer, soak testing) already tracked in each plugin's README.

## Truth label

This pass is **source + architecture work**, validated by reasoning and a portable unit check of
the dependency-light core; it has not been compiled or `auval`-validated on macOS. Treat every
plugin as an **internal build** until it is rebuilt, `pluginval`/`auval`-checked, and
DAW-listened on a Mac per the existing build standard.
