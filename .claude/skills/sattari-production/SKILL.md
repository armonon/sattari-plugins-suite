---
name: sattari-production
description: Governs engineering work inside Sattari Suite. Use for any change affecting Compass, Key, Auto Pitch, Maqam, Stack, Arp, Rhythm Genie, VoxKey, VoxSynth, Chop, Granulator, Scaler, Vocal, Mix, Create, their modules, SattariModule, rack hosts, the Session Bus, JUCE lifecycle, plugin state, audio processing, plugin UI, presets, AU/VST3, validation, release engineering, performance, or audio testing. Turns a request into requirement, evidence, implementation, verification, product truth. StemDeck is excluded unless explicitly included.
---

# Sattari Production

## Purpose

This skill governs engineering work inside Sattari Suite. It coordinates
specialist skills while enforcing Sattari-specific architecture, audio quality,
real-time safety, validation and release standards.

Its job is to turn a request into:

```text
REQUIREMENT
    ↓
EVIDENCE
    ↓
IMPLEMENTATION
    ↓
VERIFICATION
    ↓
PRODUCT TRUTH
```

Do not optimize merely for task completion.
Optimize for reliable shipping software.

## Trigger

Use this skill whenever work affects:

Compass · Key · Auto Pitch · Maqam · Stack · Arp · Rhythm Genie · VoxKey ·
VoxSynth · Chop · Granulator · Scaler · Vocal · Vocal modules · Mix ·
Mix modules · Create · Create modules · SattariModule · rack hosts ·
Session Bus · JUCE lifecycle · plugin state · audio processing · plugin UI ·
presets · AU/VST3 · validation · release engineering · performance ·
audio testing

StemDeck is excluded unless the user explicitly includes it.

## Strategic Context

Sattari should be treated as six product surfaces backed by a modular platform:

```text
Compass        KNOW
Auto Pitch     TUNE
Stack          MAKE
Vocal          SING
Create         TRANSFORM
Mix            CONTROL
```

Current default focus:

```text
CORE=
Compass
Key
Auto Pitch
Stack
Arp
Rhythm Genie
Sub Conjurer
Vocal
```

Do not create additional plugins unless explicitly requested.
Prefer depth over catalog expansion.

## Phase 1 — Classify the Task

Before editing, classify the work. Choose all applicable categories:

```text
BUG
DSP
REALTIME
ARCHITECTURE
COMPASS
STATE
UI
PERFORMANCE
VOICE_MIDI
DATASET
PLUGIN_VALIDATION
HOST_COMPATIBILITY
RELEASE
RESEARCH
```

This determines which specialist skills should be loaded.
Do not invoke irrelevant skills.

## Phase 2 — Route Specialist Skills

**BUG** — use `systematic-debugging`, then apply `test-driven-development` when
deterministic regression protection is practical.

**DSP / JUCE** — use `juce-plugin` and relevant DSP references. Review:
algorithm, parameter semantics, numerical stability, sample-rate behavior,
block-size behavior, audio-thread behavior, latency, bypass, reset, state.

**ARCHITECTURE** — use `improve-codebase-architecture`. Evaluate whether the
change strengthens shared modules, creates duplication, increases coupling,
violates rack boundaries, bypasses SattariModule, introduces plugin-specific
infrastructure, or weakens test seams. Do not refactor merely for aesthetics.

**PLUGIN VALIDATION** — use `plugin-testing`. Apply relevant build, scan,
pluginval, AUVal, instantiation, lifecycle and DAW smoke tests.

**VOICE → MIDI** — use `voice-note-to-midi` as a research and benchmarking
reference. Never automatically make its Python dependencies runtime
dependencies.

**DATASET / STEM PREPARATION** — use `media-demucs` for offline research/test
preparation where appropriate. Do not introduce Demucs into shipping plugins
unless explicitly required.

**UI / PRODUCT DESIGN** — use `frontend-design` for hierarchy, visual
direction, interaction ideas, design critique and coherence. Implement using
the repository's existing UI technology unless migration was explicitly
requested.

**MIX / MASTERING LISTENING** — use `mix-engineer` and `mastering-engineer` as
review perspectives. Do not treat their suggested EQ curves, loudness targets
or genre conventions as mandatory DSP specifications.

**PERFORMANCE** — profile first. Do not optimize blindly. If actual CUDA
kernels exist and CUDA profiling is the task, a CUDA tuning skill may be used.
Otherwise do not invoke CUDA tooling.

**RELEASE / COMPLETION** — use `verification-before-completion` and
`requesting-code-review` before strong completion claims.

## Phase 3 — Inspect Before Editing

Read enough surrounding implementation to understand: owning module,
interfaces, parameter flow, processing graph, state, rack integration, context
integration, tests, release metadata, dependent processors.

Search for existing implementations before creating new ones. Ask:

```text
Can this behavior live in an existing shared abstraction?
```

If yes, strongly prefer that solution.

## Phase 4 — Define Success

Before implementation, convert the request into observable outcomes.

```text
REQUEST
Fix clicks when Echo feedback changes.

SUCCESS
- reproduce click
- regression test or deterministic render exists
- parameter changes do not produce discontinuities
- existing feedback behavior preserved
- no RT allocation introduced
- CPU does not materially regress
```

Do not use vague success criteria such as "works better", "sounds
professional", "cleaner", "optimized". Turn them into measurements or explicit
listening requirements.

## Phase 5 — Establish Baseline

For bug, DSP, performance and regression work, capture the old behavior first
when practical: failing unit test, failing contract test, audio render, CPU
profile, pluginval failure, host reproduction, state mismatch, spectral
measurement, pitch metrics, screenshot/render check.

Never claim improvement when no meaningful before/after evidence exists.

## Phase 6 — Implement

Prefer `SMALLEST DEEP CHANGE` over `LARGEST VISIBLE CHANGE`.

Preserve architectural boundaries, parameter IDs (unless intentionally
migrating) and state compatibility where possible. Avoid unnecessary
dependencies, duplicated DSP, audio-thread hazards and unrelated cleanup.

If architecture must change, explicitly state why the request cannot be solved
cleanly without it.

## Phase 7 — DSP Safety Review

Whenever audio processing changes, inspect:

```text
[ ] NaN            [ ] Inf              [ ] divide by zero
[ ] denormals      [ ] unstable feedback[ ] unstable coefficients
[ ] clipping       [ ] DC               [ ] discontinuities
[ ] zipper noise   [ ] wet/dry alignment[ ] latency
[ ] bypass         [ ] reset            [ ] silence
[ ] tails          [ ] mono             [ ] stereo
[ ] channel layout [ ] sample-rate assumptions
[ ] block-size assumptions
```

Where appropriate test 44.1k / 48k / 88.2k / 96k / 192k and unusual blocks
1, 2, 7, 16, 31, 32, 63, 64, 127, 128, 255, 256, 511, 512, 1024, 2048.

Do not require every matrix cell for every trivial change. Select tests
proportionate to risk.

## Phase 8 — Real-Time Safety Review

Any path reachable from the audio callback should be inspected for:

```text
[ ] allocation     [ ] deallocation     [ ] mutex
[ ] blocking wait  [ ] filesystem I/O   [ ] networking
[ ] logging        [ ] GUI access       [ ] lazy initialization
[ ] object construction                 [ ] model loading
[ ] unbounded loops                     [ ] exceptions
```

When unsure, investigate rather than assume safety. Prefer preallocation,
atomics, lock-free FIFO, double buffering, immutable snapshots, prepared state.

## Phase 9 — Compass Review

If Compass or context-aware behavior changes, test:

```text
[ ] context present   [ ] context missing   [ ] low confidence
[ ] stale context     [ ] context changes   [ ] user override
[ ] host override     [ ] session restore   [ ] multiple instances
[ ] version compatibility
```

Rule:

```text
COMPASS MAY ENHANCE.
COMPASS MUST NOT CREATE FRAGILE BASIC FUNCTIONALITY.
```

A standard processor should have sane behavior when context is unavailable.

## Phase 10 — State Review

If parameters, presets, processors or modules change, verify relevant:

```text
[ ] save            [ ] reload          [ ] old preset
[ ] new preset      [ ] automation      [ ] duplicate
[ ] remove/reinsert [ ] sample-rate change
[ ] project reopen  [ ] missing data    [ ] invalid data
```

Never casually rename stable parameter IDs. If migration is required,
implement an explicit migration path.

## Phase 11 — Performance Review

Never use "seems faster" as evidence. Measure. Where applicable collect:
median, p95, p99, max, allocation count, memory, startup, per-instance CPU,
multi-instance CPU.

Compare baseline and candidate. If no formal budget exists, report the numbers
instead of inventing one. A measurable regression must be disclosed.

## Phase 12 — Audio Regression

Signals: silence, impulse, sine, sweep, noise.
Musical sources: voice, bass, drums, instrument, full mix.
Possible metrics: peak, RMS, LUFS, DC, spectral delta, THD, phase, correlation,
latency, difference energy, pitch error, onset error.

Choose metrics based on algorithm intent.

## Phase 13 — Subjective Listening

For sound-quality claims, create listening evidence. Claude may prepare renders,
A/B files, ABX files, randomized labels, listening forms, parameter snapshots
and metric summaries.

Claude may NOT fabricate human judgments. If human listening has not occurred:

```text
LISTENING STATUS: NOT VERIFIED
```

## Phase 14 — Plugin Validation

```text
BUILD → UNIT / CONTRACT → PLUGIN VALIDATOR → HOST → STATE → AUDIO
```

Potential gates: AU build, VST3 build, pluginval, AUVal, scan, instantiate,
process, destroy, reload.

Use strict plugin validation for release-critical work. Never equate validator
success with final product approval.

## Phase 15 — Host Matrix

For release-critical plugins, validate relevant DAWs: Logic, Ableton Live,
REAPER, Studio One, Cubase, FL Studio, Bitwig.

Only claim hosts actually tested. Important cases: scan, instantiate,
duplicate, automation, tempo changes, transport jumps, loop, freeze, bounce,
offline render, project save, project reload, editor open/close, multiple
instances.

## Phase 16 — UI Review

```text
[ ] parameter display correct   [ ] edit correct     [ ] automation reflected
[ ] preset reflected            [ ] state restore    [ ] focus
[ ] pointer interaction         [ ] keyboard interaction
[ ] scaling                     [ ] resizing         [ ] high-DPI
[ ] long labels                 [ ] disabled state   [ ] error state
[ ] loading state               [ ] repeated editor open/close
```

Do not allow a visually attractive UI to hide incorrect parameter behavior.

## Phase 17 — Code Review

For meaningful changes, perform an independent review after implementation:
correctness, architecture, RT safety, DSP, state, performance, test coverage,
compatibility.

The implementer should not be the only source of approval.

## Phase 18 — Release Gate

For lifecycle promotion, evaluate: build, tests, DSP, realtime, plugin
validation, hosts, state, performance, audio regression, listening, UI,
packaging, signing, notarization, clean install.

Return one of `PASS` / `BLOCKED` / `NOT VERIFIED`.
Never substitute optimistic language.

## Lifecycle Rules

**experimental → alpha** — coherent implementation, basic tests, stable basic
operation.

**alpha → validation** — meaningful automated coverage, DSP/behavior
validation, regression evidence, plugin validation, known limitations
documented.

**validation → release-candidate** — target host testing, state/reload
confidence, performance evidence, representative audio corpus, listening
evidence where relevant, no unresolved critical defects.

**release-candidate → shipping** — release gate pass, distribution validation,
signing/notarization where required, clean install, evidence-backed release
record.

Do not promote lifecycle merely because someone changed a label.

## Special Flagship Rules

**Auto Pitch** — require representative vocal testing. Corpus categories:
male, female, low, high, breathy, vibrato, falsetto, rap, legato, slides,
short notes, noisy, transparent tuning, hard tuning, microtonal where
supported.

**Key / Compass** — track analysis accuracy using appropriate ground truth.
Confidence must have semantics. Do not present unreliable analysis as
certainty.

**VoxKey** — benchmark note-level transcription where possible. Evaluate
precision, recall, onset, offset, octave, latency, legato, repeated notes,
noise.

**Sub Conjurer** — require objective low-frequency analysis plus listening
evidence. Pay particular attention to phase, DC, headroom, speaker-risking sub
energy, harmonic generation, mono compatibility.

## Do Not

Do not create new plugins by default; migrate framework without explicit
intent; introduce Visage merely because a Visage skill exists; introduce CUDA
without a CUDA requirement; add PostgreSQL to solve local plugin problems; use
LLM fine-tuning for ordinary audio DSP; make offline research tools runtime
dependencies by accident; claim listening results Claude did not hear through
an approved evaluation process; rewrite architecture to satisfy one edge case;
hide failures; weaken tests to make CI green; silently reduce validation
requirements.

## Failure Handling

If a test exposes a real defect: fix the defect. Do not weaken the test unless
the test is demonstrably incorrect.

If validation cannot be performed: report `NOT VERIFIED` and specify exactly
what remains.

If an external dependency is unavailable: continue all independent validation
possible.

## Output Format

At the end of meaningful engineering work, return:

```text
SATTARI ENGINEERING REPORT

TASK
<what was requested>

ROOT CAUSE / DESIGN
<what actually mattered>

IMPLEMENTED
- ...

VERIFIED
- ...

MEASUREMENTS
- ...

ARCHITECTURE
- ...

NOT VERIFIED
- ...

RISKS
- ...

LIFECYCLE IMPACT
<experimental / alpha / validation / RC / shipping>

STATUS
<PASS / BLOCKED / NOT VERIFIED>
```

Do not inflate the status.

## Production Readiness Language

These phrases require evidence appropriate to their scope: "production ready",
"release ready", "shipping", "validated", "fully fixed".

- Only implementation tests performed → `IMPLEMENTATION VERIFIED`
- Plugin validators also run → `PLUGIN VALIDATION VERIFIED`
- Human listening outstanding → `LISTENING NOT VERIFIED`

Precision in language is part of engineering quality.

## Master Principle

The Sattari production agent is not rewarded for generating the most code.
It is rewarded for producing:

```text
LESS UNCERTAINTY
MORE EVIDENCE
BETTER SOUND
FEWER FAILURES
STRONGER ARCHITECTURE
BETTER MUSICAL EXPERIENCE
```

When in doubt: measure it, test it, listen to it, or mark it unverified.
