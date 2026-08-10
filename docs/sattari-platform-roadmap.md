---
type: program-plan
category: sattari
area: audio-platform
status: active
created: 2026-06-17
updated: 2026-08-09
source: Platform strategy — Core once, StemDeck as the forcing flagship
---

# Sattari Audio — Platform Roadmap

The strategy: stop solving a seven-way breadth problem and build **one platform plus
sequenced products**. The engine, packaging, UI, and licensing get built once;
StemDeck's hard DSP becomes reusable parts for everything after it.

This document is grounded in what the repository can actually prove today, not in
intentions. Truth labels are kept honest on purpose.

## Premise check (with receipts)

The plan rests on "StemDeck passes pluginval at strictness 5 — the packaging path is a
proven asset." Verified against the repo:

- **True:** `plugins/stemdeck/logs/2026-05-18-validation.log` shows a real pluginval run at
  `Strictness level: 5` on the StemDeck VST3, plus codesign verification. The
  packaging/hosting **plumbing is real** and is exactly the asset to lift into Core.
- **But pluginval@5 proves hygiene, not sound.** It checks no-crash, state save/restore,
  parameter automation, editor-while-processing, and allocation/thread behaviour — not
  whether the time-stretch or key detection sound good.
- **StemDeck remains the least externally validated of the suite, but it is no longer
  mostly scaffold.** It now has spectral-flux + DBN/Viterbi-style beat/downbeat
  analysis, variable-tempo Anchor/Groove Sync, optional neural beat/key refinement,
  native ONNX and Demucs separation paths, deterministic intent capture/replay, and
  an editable arrangement workspace. AU is now part of the declared build formats.
  The open gap is evidence and productization: licensed real-song accuracy, physical
  hardware, broad third-party hosting, DAW certification, model redistribution,
  commercial time-stretch licensing, and notarized clean-machine installation.

So the conclusion holds with a sharpened framing: **the packaging path is proven; the DSP
is not.** StemDeck's job as flagship is to make the hard DSP real — which is precisely why
it is the right forcing function, and precisely why it is not "the hardest piece already
done."

## The platform: Sattari Audio Core

Core is built in three honest layers.

**Have today (shipped in this work):**

- Shared C++ core (`core/include/sattari_audio_core/`): brand registry + palette, one
  `SattariLookAndFeel`, the cross-plugin `SattariPresetManager`, and realtime-safety
  utilities (`AtomicSnapshot`, `ScratchBuffer`). All seven editors consume it.
- **Packaging path, now lifted out of per-plugin boilerplate:** `cmake/SattariPlugin.cmake`
  exposes `sattari_add_plugin()` — one call wraps `juce_add_plugin` with the Sattari
  defaults (AU+VST3+Standalone by default, shared-core link, recommended flags,
  copy-after-build). Sub-Conjurer is the reference adopter; the other six collapse to the
  same call. `scripts/validate_plugin.sh` is the reusable codesign+auval+pluginval@5 gate.
- CI (`.github/workflows/ci.yml`) and the brand-registry/Hub sync check keep it honest.

**Build next (the real engine work StemDeck forces):**

- A small **audio engine** layer: transport, sample-rate/format handling, a lock-free
  loaded-audio swap (StemDeck's `DeckPlayer` already does this — generalise it).
- A **DSP primitives** library, promoted from existing plugin code (see inventory below)
  so Auto Pitch, Sample Library Brain, and StemDeck share one implementation each of
  pitch detection, key/chroma analysis, and time/pitch.

**Greenfield (no code exists yet — scope as net-new, not extraction):**

- **Licensing / activation / payment.** The Hub does install + receipts only; there is no
  license, EULA, entitlement, or activation code anywhere in the repo. This is a build,
  not a lift, and it gates the first paid release.

The trap to avoid (named in the strategy, restated as a rule): **Core's first job is to make
StemDeck's hard DSP real, not to be elegant scaffolding.** Promote a primitive into Core
only once a real product needs it and it works on real material.

## Reusable DSP primitive inventory (what's actually real)

| Primitive | State today | Lives in | Feeds |
|---|---|---|---|
| Pitch detection (YIN) | Real, good | VoxKey `PitchDetector` | Auto Pitch, Sample Library Brain |
| Key / chroma + energy | Real DSP baseline + optional neural refinement | StemDeck `TrackAnalysis` / `NeuralKeyEngine` | StemDeck live-key, Sample Library Brain similarity |
| Time/pitch (key-lock) | Real via **RubberBand**; granular fallback smears | StemDeck `DeckPlayer` | StemDeck, Auto Pitch |
| Stem separation | Native ONNX engine + Demucs command fallback; distributable weights unresolved | StemDeck `NeuralStemEngine` / `StemProvider` | StemDeck |

Two of the three "StemDeck primitives" the strategy counts on (time-stretch, key) are real
enough to promote; **separation is a wrapper around an external tool**, not a Sattari asset,
and shouldn't be counted as one.

## Decisions to make now (they gate the build)

1. **RubberBand license.** The only listenable time/pitch path links RubberBand (GPLv2 or a
   paid commercial license). Closed-source paid StemDeck ⇒ buy the commercial license or
   replace the engine. Decide before Build mode — it is load-bearing for the whole line
   (Auto Pitch wants it too).
2. **AU host certification.** StemDeck now declares AU+VST3+Standalone; the Debug AU
   builds, receives an ad-hoc signature, and passes `auval -v aumu Sd01 Satr` on
   2026-08-09. Complete Logic/GarageBand save/restore and automation tests, then clear
   the release-signing warning with real Developer ID credentials.
3. **Separation packaging.** "Optional local Demucs" is a dealbreaker for normal users.
   Choose: bundle a one-click on-device separator, or ship pre-separated stem packs first.
4. **Licensing technology.** Pick the activation/license approach (e.g. signed offline
   license files vs. a hosted check) before the first paid product, and build it in Core.
5. **Open vs. closed source** per product — it interacts with (1).

## Flagship: StemDeck — "a DAW designed by a DJ"

The defensible wedge is the **bridge neither parent fills**: DJ apps won't let you keep and
build on what you create; DAWs won't let you perform fluidly. StemDeck = perform a live
stem remix, then refine it into a release. Two modes plus the bridge:

- **Perform** (DJ half): four decks, stems on faders, beat-sync, live key, loops, cues,
  pads, mic monitor. Mostly scaffolded.
- **Build** (Ableton half): captured clips you arrange, warp, trim, layer, light FX, export.
- **Capture bridge** (the actual differentiator): hit record → loops, swaps, key shifts,
  takes land in Build as editable clips.

Phases — order matters because a DJ tool lives or dies on these:

1. **Make the DJ half real — IMPLEMENTED / NEEDS EXTERNAL CERTIFICATION.** Rock-solid tempo sync that doesn't drift over minutes,
   accurate live key, listenable time-stretch — validated on known-BPM/known-key material
   with the listening matrix already scoped in the gates doc. **True scope flag:** real DJ
   sync now has automatic onset/beat/downbeat analysis, variable-tempo warp markers,
   per-deck sync enable, manual correction, and deterministic contracts. Remaining:
   waveform-direct downbeat anchoring plus the licensed real-music/long-drift/controller
   matrix. A full draggable multi-marker warp editor remains; no accuracy claim until
   that corpus passes.
2. **Bundle separation properly — ENGINE + USER LIFECYCLE COMPLETE / DISTRIBUTION DECISION OPEN.** Native
   ONNX separation, visible progress/cancel, cache handoff, streaming preview, and
   Demucs fallback exist. Zero-setup shipping still requires a commercially cleared
   model/runtime package or a first-run licensed download.
3. **Build the capture bridge — IMPLEMENTED.** Typed, versioned intent events, pinned
   analysis, stable channel/insert identity, printed safety lanes, editable source
   lanes, 60-second retrospective master recovery, performance scenes, persisted
   alternate takes, and sample-identical fixed-block replay are enforced by contracts.
4. **Build mode — FUNCTIONAL ALPHA.** Arrange/trim/fade/layer, automation, MIDI/piano
   roll, lane DSP, master automation replay, portable sessions, and export exist.
   Remaining product work is region-level comp splicing, long-session editing
   performance, hosted-plugin replay certification, and real artist workflow
   testing—not a greenfield DAW build.
5. **Identity, packaging, launch — PARTIAL.** Lock the "perform → keep → build → release" story;
   one-click install + license (from Core); make the demo the marketing — live four-stem
   remix, stop, export a finished track in 30 seconds.

**The one discipline:** don't try to out-djay djay/Serato/rekordbox/VirtualDJ — you won't win
on performance. StemDeck wins only on the bridge. If the capture-and-build isn't great, no
one switches. Obsess over that; let everything else be merely good enough.

## Product sequence

1. **Core + StemDeck** — Core proven by making StemDeck's DSP real.
2. **Sample Library Brain** — best next problem: reuses the engine's analysis (YIN +
   chroma/energy already exist) for similarity search; **no DAW to build**, so it is the
   most contained candidate for first *revenue* even though StemDeck is the flagship.
3. **One MIDI tool — MIDI Genius** (Scaler proves the category) **or Auto Pitch.** One at a
   time.

**Cut the dead weight:** shelve **HookForge** (weakest concept-market fit) and **Loop Doctor**
(you will not out-DSP iZotope RX — a 15-year moat) unless one surfaces a real wedge. The
"Audio Suite" is **not a product** — it's the install Hub once there's more than one thing to
install (already true; the Hub exists).

## Where this already stands

Done and paying forward: shared Core, unified editors, Hub/brand drift guard, CI,
shared packaging, StemDeck's perform/capture/build architecture, deterministic replay,
and local alpha installers. The next concrete moves are no longer broad feature work:
they are the **RubberBand + model-weight license decisions**, the licensed real-music
accuracy/listening corpus, physical controller/interface testing, third-party plugin
isolation, AU/VST3 DAW certification, and a credentialed notarized clean-machine release.

## Truth label

This roadmap is **planning + already-shipped source/packaging work**, verified by structure
and reasoning. No DSP here has had a real-material listening pass. Every product stays an
**internal build** until it is rebuilt, `pluginval`/`auval`-checked via
`scripts/validate_plugin.sh`, and DAW-listened on a Mac per `docs/plugin-build-standard.md`.
