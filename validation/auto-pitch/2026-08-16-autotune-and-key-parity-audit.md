---
type: product-parity-audit
products: Sattari Auto Pitch + Sattari Key
date: 2026-08-16
candidate_revision: RC2 working tree based on 767becadb7cd; uncommitted
truth_label: internal engineering assessment; not a sale or equivalence claim
status: strong prototype / validation candidate; not AutoTune Pro parity
---

# Auto Pitch + Sattari Key — professional parity audit

## Executive judgment

The product concept is correct: detect session tonality once, distribute it to
every tuner, and let each vocal retain a local override. RC2 closes the prior
pass-through, reconstructed-path PDC, detector-fusion and split-key-engine
failures. Professional parity is still blocked by four fundamentals:

1. Real-vocal sound quality has not been scored.
2. The shipping tuner has one 113.08 ms print engine, not a live engine.
3. The shared key decision needs a wider held-out, genre-balanced calibration set.
4. Host workflows (ARA, automation, multi-instance, Graph recall) have not
   completed the DAW matrix.

Adding more controls before those four are closed would increase surface area,
not quality.

## What is already real

- Embedded CREPE-tiny plus YIN pitch detection.
- Rubber Band monophonic correction with formant, throat, vibrato, Flex,
  Humanize, transition, reference tuning and MIDI targeting.
- Five automatic styles and an intentional hard/classic effect path.
- Persistent Graph note objects with retarget, bypass, split/join/delete,
  per-object controls, undo/redo, zoom and optional time correction.
- Manual key, live AutoKey, Sattari Key bus following, file-based Key Map and
  sidechain/full-mix analysis.
- ARA2 build foundation, AU/VST3/Standalone builds and an optional AAX lane.
- Synthetic correction, CPU, transition, polyphonic and safety coverage.
- Six cleared vocal clips and an 18-item blinded listening package per candidate.

## RC2 hardening completed after this audit began

- Rubber Band is explicitly R3/Finer; the old build accidentally remained on R2.
- Correction Off and true zero Amount use an untouched delay-aligned dry reference.
- The five-case strict null is `-inf` dBFS at zero-sample offset.
- Host latency is 5,428 samples at 48 kHz; forced-unity R3 and genuinely tuned
  onset measurements are within 0.6 ms of the report.
- YIN and CREPE use conservative agreement/octave/continuity fusion; neural pitch
  cannot blindly replace a valid YIN estimate.
- Auto Pitch's frozen internal fallback remains the Aarden/chroma decision with
  its 0.10 release threshold. Sattari Key uses a specialized balanced full-mix
  detector and hands Auto Pitch only an explicitly locked session-bus result.
  Low-confidence input still yields **No Result** and cannot retune a vocal.
- The held-out-losing neural key model is disabled in both release paths.
- Legacy live-engine parameters remain session-compatible but are visibly inactive
  and non-automatable.

## Current blockers

### 1. Sound quality is unproven

The synthetic benchmark shows excellent target math, not transparent vocals.
The six real-vocal sets still have no human scores for naturalness, artifacts,
vibrato, transitions, consonants, timbre or preference. There is also no
level-matched AutoTune Pro reference in the package, so competitive quality has
not been measured.

### 2. Live performance is not shipping

`liveEngine` and `lowLatency` are host-visible parameters, but the processor pins
`useLiveEngine = false`. The current moving-period PSOLA implementation produces
grain on ordinary vibrato and must not simply be unlocked. Studio/Rubber Band is
the only reachable monophonic path.

### 3. PDC and pass-through are machine-safe; hosts remain unqualified

The former 2,048/2,080 mismatch and -43.8 dB pass-through residual are closed in
RC2. Dry/zero-Amount nulls are sample-identical, and the reconstructed R3 path is
reported within 0.6 ms. This does not replace Logic/Ableton/REAPER PDC, bypass,
automation and offline-bounce testing.

### 4. The key handoff is unified; accuracy is not broadly calibrated

Auto Pitch retains the held-out Aarden fallback and both products quarantine
`keynet_spec` after its 37.5% held-out exact score lost to Aarden's 85.4%.
Sattari Key's specialized model-free full-mix engine now reaches 26/26 key/mode
agreement with TuneBat through the native evaluator and plugin file path, versus
the previous 7/26 primary and 15/26 top-two. It exposes a ranked alternate,
tempo and an explicit frozen session lock. Evidence breadth remains the limit:
the 26 files are related development material, not labeled ground truth, and
confidence is not calibrated across contemporary genres. Universal parity is
therefore not claimed.

### 5. Workflow parity is incomplete

Sattari Key 0.3.0 supports live listen, asynchronous whole-song analysis, file
selection and drag/drop, manual override, Camelot notation, ranked alternate/
relative-key review, file/live tempo and explicit locked suite publication. It
does not yet include an audition keyboard or explicit key-change timeline
automation. Auto Pitch lacks freehand/
line pitch contour tools and a true multi-track editor. ARA and AAX are
foundations, not qualified shipping workflows.

## The intended combined flow

1. Put Sattari Key on the instrumental/master, or drop a supported audio file.
2. It returns primary key, relative alternative, decision margin, tuning
   reference and tempo; uncertainty produces **No Result**, not a confident guess.
3. The user auditions the scale, confirms or overrides it, then presses **Lock
   Session**.
4. One versioned session message publishes the locked global key or a key-change
   timeline. Every Auto Pitch instance follows it automatically.
5. Auto Pitch uses the vocal only for pitch/voicing/note segmentation. A local
   key override remains local and is visibly marked.
6. Auto Mode serves tracking and performance through a genuinely low-latency
   engine. Studio/Graph mode uses full-context high-quality correction.
7. A unified multi-track view switches among vocal instances while keeping the
   session key visible and authoritative.

## Build order

### Gate A — establish sound truth

- Complete the existing 18-item user listening sheet.
- Add AutoTune Pro 11 reference renders of the same cleared material and matched
  settings, level-match them, reseal the key, and run a blind non-inferiority
  comparison.
- Do not claim parity unless Sattari is statistically non-inferior on artifacts,
  naturalness, timbre, transitions and preference.

### Gate B — fix release correctness — COMPLETE IN RC2

- Publish measured R3 group delay and align the untouched dry path to it.
- Give correction-off and Amount=0 a strict sample-identical null.
- Mark legacy Live and Low-Latency controls inactive/non-automatable while
  preserving old-session deserialization.

### Gate C — build the sound engine, not more knobs

- Fuse YIN and CREPE with explicit voicing, octave-error rejection and temporal
  continuity instead of making the user choose a detector blindly.
- Add a note/gesture model that distinguishes stable pitch, vibrato, slides,
  scoops, consonants, breaths and note transitions.
- Drive correction from a smoothed per-note pitch centre while preserving
  vibrato rate/depth and protecting unvoiced/transient regions.
- Replace the failed moving-period PSOLA path with a continuous epoch/phase model
  and prove it on vibrato before exposing Live mode.
- Keep a separate full-context Studio/Graph renderer for maximum quality.

### Gate D — make Sattari Key authoritative — PRIVATE DEVELOPMENT TARGET PASS; CALIBRATION OPEN

- Keep Auto Pitch's frozen internal fallback isolated; use the session bus for
  explicitly locked Sattari Key full-song decisions. Migrate remaining publishers
  only after their lifecycle gates permit it.
- Keep neural override disabled until a held-out pop/rock/electronic corpus supports it.
- Preserve shared source-specific confidence; never map validity to `1.0`.
- Calibrate probabilities on held-out data. Top-2/relative alternatives plus
  **No Result** are implemented, but the displayed margin is correctly labeled
  uncalibrated.
- Extend the bus schema with decision kind (manual/detected), calibrated
  confidence, publisher, scope and optional key-map timeline.
- Listen, File and Manual modes, file drag/drop, real tempo detection, top-two
  review and explicit **Lock session** publication are implemented in Sattari Key
  0.3.0. Still add an audition keyboard and explicit key-change timeline.

### Gate E — finish professional workflow

- Freehand curve and line tools with selection-safe undo.
- ARA correction/state round-trip in Logic, Studio One and Cubase/Nuendo.
- Multi-track vocal switching and shared session context.
- AU/VST3 host matrix first; AAX/Pro Tools remains an external SDK/host gate.

## Definition of done

Parity is earned only when all of these are true:

- Blind real-vocal comparison is non-inferior to AutoTune Pro on the declared
  corpus and settings.
- Live mode is genuinely monitorable and Studio mode is transparently printable.
- Actual and reported latency match at every supported sample rate and block size.
- Sattari Key beats the declared held-out accuracy floor, confidence calibrates,
  and every follower receives the same decision.
- State, automation, bypass, transport, offline render and multi-instance behavior
  pass in the supported DAWs.
- The UI never offers a control whose DSP is disabled and never presents an
  uncalibrated verdict as certainty.

That is a professional product claim. Anything earlier is an internal test build.
