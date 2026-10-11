# Sattari Product Review And Upgrade Plan

Date: 1 October 2026. Reviewed source revision:
`204396586ea633fcf4973ff7a807f5e3e79506f8`.

## Consolidation Follow-Up

The 2026-10-02 implementation is recorded in `suite-consolidation-2026-10-02.md`.
It adds the full Vocal/Create rosters, four Modular sources, Chop, preset-file
import and the focused catalog/Hub/site portfolio. Counts and gaps below describe
the earlier reviewed snapshot; unresolved parity and release gates remain explicit
in the newer record.

## Recommendation

Within this group, build around three distinct offerings: **Modular FX plus
Modular Instrument as one platform, Royal Chain as the immediate vocal strip,
and Mix as the session-level mixing assistant**. Develop Vocal and Create first
as curated module and preset collections for Modular, not as additional generic
racks competing with it. Keep existing plugins compatible and available for
existing sessions. Do not remove or rename plugin identities as a shortcut.

This is a proposed direction, not an executed product retirement, entitlement
change or source migration. Auto Pitch and other products outside this review
retain their separate release decisions.

The comparison needs one correction: Create is an audio effects rack, not a
playable instrument. Its closest overlap is Modular FX. **Stack is the closer
comparison to Modular Instrument**, so Stack is included here as an adjacent
offering. Different DAW plugin roles justify FX and Instrument binaries, but
do not require duplicating the whole product-development effort.

## Critical Findings

### Product Differentiation

Mix, Vocal and Create use the same eight-slot rack/editor foundation with
different rosters. Mix adds a genuinely different linked-session workflow.
Vocal and Create primarily add different DSP choices and presets. This is an
engineering fact; the judgment that they are weak separate flagship propositions
is a product inference, not a measured customer preference.

Actual roster overlap with Modular's nine effects:

| Offering | Shared effects | Effects absent from Modular |
| --- | --- | --- |
| Vocal | 6 of its 10: Clean, De-Ess, EQ, Comp, Heat, Space | Gate, Double, Face, Glass Choir |
| Create | 4 of its 10: Echo, Heat, Pulse, Space | Riser, Shimmer, Warble, Warp, Vocoder, 808 |
| Mix | 7 of its 14: Clean, Comp, De-Ess, EQ, Heat, Image, Pulse | Gate, Multiband, Meter, Peak, Transient, Side Chain Master, Align |

An overlap count measures roster reuse, not feature parity or quality. The
older racks expose full module parameter sets; Modular exposes only selected
controls. Royal Chain uses its own fixed-chain processors, so a similarly named
stage is not automatically the same DSP as a Modular module.

Sources: [shared editor](../core/include/sattari_audio_core/SattariRackEditor.h),
[Vocal roster](../plugins/vocal/src/PluginProcessor.cpp),
[Create roster](../plugins/create/src/PluginProcessor.cpp),
[Mix roster](../plugins/mix/src/PluginProcessor.cpp),
[Modular mapping](../plugins/modular/src/PluginProcessor.cpp).

### High Risk During Consolidation

1. **The parameter models are incompatible without deliberate migration.**
   Older rack parameters are module-namespaced, with one DSP instance per type;
   subsequent active duplicates are skipped. Modular has independent instances
   but only eight normalized controls per node. Reordering changes what existing
   slot automation controls. A preset converter cannot, by itself, rewrite a
   DAW's saved automation envelopes. Preserve old plugin IDs, indices, state and
   rendering behavior; design migration before swapping engines. See the
   [legacy rack contract](../core/include/sattari_audio_core/SattariRackProcessor.h)
   and [Modular parameter layout](../plugins/modular/src/PluginProcessor.cpp).
2. **Simply importing every module into Modular can increase costs even in an
   empty rack.** The constructor creates every allowed module type per slot.
   Preparation prepares them all and chooses the maximum available effect
   latency, then reports eight times that budget. Fixed latency avoids changes
   during editing, but adding one higher-latency effect can affect every chain.
   Benchmark initialization, memory and latency before growing the library;
   this is a source-confirmed scaling risk, not a measured performance failure.
   See `Impl::Impl` and `prepareToPlay` in the
   [Modular processor](../plugins/modular/src/PluginProcessor.cpp).

### Royal Chain Consistency And Control

3. **The host and editor expose different factory preset banks.** The processor
   registers eight programs; the editor has eleven separately hardcoded presets.
   The shared name Podcast Clean also has different settings. Editor preset
   Hard Tune Hook sets Tune to 0.35, whereas the processor bank sets Tune to zero
   throughout. Unify preset definitions and route both selectors through the
   same loading path. The earlier explanation that all factory presets leave
   Tune off was too broad: it described only the processor bank.
   Sources: [processor presets](../plugins/royal-chain/src/RoyalChainProcessor.cpp),
   [host programs](../plugins/royal-chain/src/RoyalChainProcessor.h),
   [editor presets](../plugins/royal-chain/src/RoyalChainEditor.cpp).
4. **Zero does not reliably mean off.** Royal contributes to Width and Space
   independently of their own knobs, and Finish always applies a nonlinear
   output stage. This may be intentional voicing, but the current controls have
   no explicit per-stage bypass contract. Add clear bypasses and level-matched
   comparison, preserving existing presets' legacy sound. Do not silently change
   all zero-value behavior in old sessions.
   Sources: [Width](../plugins/royal-chain/src/WidthModule.cpp),
   [Space](../plugins/royal-chain/src/SpaceModule.cpp),
   [Finish](../plugins/royal-chain/src/FinishModule.cpp).
5. **The Width stage does not create width from a mono vocal.** It returns early
   for mono buffers. With identical left/right input and histories, its side
   terms remain zero. It modifies existing stereo differences rather than
   generating an independent double. Separate stereo-width and vocal-doubling
   promises; add a qualified mono-to-stereo mode only if that is the intended
   product behavior. This conclusion follows from the source equations; a new
   audio render was not performed in this review.
6. **Names can overpromise the underlying treatment.** Royal Chain Clean is
   filtering/gating, and Space currently uses fixed cross-feedback delay taps.
   Neither alone establishes advanced restoration or a full reverb engine.
   Names such as Noisy Room Rescue should be backed by real recordings and clear
   limits. Test the sound before expanding visual or marketing claims.
   Sources: [Clean](../plugins/royal-chain/src/CleanModule.cpp),
   [Space](../plugins/royal-chain/src/SpaceModule.cpp),
   [editor preset names](../plugins/royal-chain/src/RoyalChainEditor.cpp).

## Reviews By Offering

### Modular FX

**Keep:** independent duplicate effects, reusable DSP, macros, scenes, A/B,
undo/redo, source recall and a coherent analog-rack direction. This is the
strongest foundation for combining the library without forcing users to open
many separate plugins.

**Criticism:** its current fixed six-slot main path plus two-slot return is a
curated rack, not arbitrary modular routing. The eight-control limit prevents
full equivalence with existing modules. No external sidechain, output limiter
or full specialist editors are present. Scenes can change module types, but
replacement resets histories; they are not a qualified seamless performance
switching system. Slot-based automation can surprise users after reordering.

**Upgrade:** retain the simple rack view, add a detailed module view, establish
stable automation/migration rules, add explicit level management and optional
output protection, then qualify the missing module collections. Benchmark
resource and latency costs before increasing the roster. Defer third-party
hosting and arbitrary patch cables until the native platform is complete.

**Decision:** primary rack platform, not yet a drop-in replacement for every
legacy rack. [Current capabilities and limits](../plugins/modular/README.md).

### Modular Instrument

**Keep:** playable layers feeding the same effects platform, embedded samples,
macros and a usable initial sound. It legitimately requires an instrument
plugin identity for DAW MIDI workflows.

**Criticism:** two sources, Granulator and Rhythm Genie, do not yet make a broad
instrument collection. The selected granular controls cover only part of that
engine. Layer blending is not equivalent to Stack's four-layer keyboard mapping,
and Chop is missing. More effects alone will not fix instrument workflow gaps.

**Upgrade:** prioritize sample/zone editing, predictable root-note and loop
controls, per-layer level/pan/key ranges, envelopes, velocity response and useful
sound content. Integrate Chop only after its source/state behavior is qualified.
Port Stack's useful capabilities before considering its retirement. More layers,
MPE and expanded modulation should follow profiling and an agreed instrument
scope, not all arrive as a first-release requirement.

**Decision:** pair with Modular FX as one product platform. Define success by
playing and editing sounds, not by how many module names fit in its menu.

### Vocal

**Keep:** straightforward vocal-chain construction, full module controls,
practical presets, and the unique Gate/Double/Face/Glass Choir choices currently
absent from Modular. Its transparent chain order is useful for engineers.

**Criticism:** six of ten modules already exist in Modular, and the remaining
four are portable library value. The one-active-instance limitation prevents
common workflows such as two independently configured compressors. There is no
integrated tuner. Another generalized rack is a weak reason to maintain a
separate flagship alongside Modular and Royal Chain.

**Upgrade:** first build a Vocal collection in Modular with full control parity,
independent stages and well-tuned dry-lead, doubled, spoken and background-vocal
chains. Preserve the current Vocal plugin for old sessions. A future standalone
Vocal product should earn its existence with a distinctive expert workflow,
such as simultaneous dynamics/EQ/de-ess views and parallel vocal paths, not just
a different module menu.

**Decision:** collection-first; do not spend the next cycle on another isolated
cosmetic overhaul. This is a proposal, not authorization to remove the plugin.

### Royal Chain

**Keep:** a fixed, fast vocal workflow, meaningful macro controls, built-in tuning
and visual feedback. Its central promise should be reaching a good vocal sound
with fewer decisions than Modular. It should not become another rack builder.

**Criticism:** preset-bank divergence, hidden Royal interactions and limited
mono widening undermine predictability. Reusing Auto Pitch's live engine is
valuable but does not inherit all Auto Pitch features or listening approval.
The visuals and preset names should not imply analysis or restoration beyond
what the DSP performs. A large visual display must earn its space by helping
users hear and understand changes.

**Upgrade:** unify presets, add explicit stage bypass with correct timing,
gain-matched A/B and clear tuning/key status. Distinguish existing stereo width
from generated doubling. Tune Clean, compression, de-essing, color and ambience
on real vocals; require sonic evidence before replacing its DSP with shared
modules. Expose optional advanced controls without abandoning the simple front
panel. Preserve precision note editing as Auto Pitch's separate job.

**Decision:** keep as a distinct flagship candidate, contingent on listening and
workflow acceptance. It must outperform assembling a preset in Modular on speed
and clarity, not necessarily on maximum flexibility.

### Create

**Keep:** six effects not yet in Modular and musically useful preset directions.
Riser, Shimmer, Warble, Warp, Vocoder and 808 are meaningful library additions.
Create is complementary to an instrument: it can process vocals, drums, samples
or that instrument's output.

**Criticism:** its current product identity is largely a different roster inside
the same legacy rack. It lacks a distinct performance/sequencing workflow that
would justify another general-purpose container beside Modular FX. Its creative
purpose does not remove the need for predictable tails, mono behavior, gain and
feedback limits.

**Upgrade:** make a Create collection for Modular first. Prioritize full controls,
tempo behavior, usable return routing, tail handling and curated transformations.
If user testing demonstrates a separate need, explore a genuinely performance-
oriented Create with rhythmic modulation lanes, controlled randomization and
qualified scene transitions. Those are proposed features, not present ones.

**Decision:** collection-first. Do not confuse it with Modular Instrument or
fund a second generic rack merely to keep the existing product count.

### Mix

**Keep:** linked-track analysis, role assignments, locks, proposals, audition and
undo. These make it different from Modular; neither a single-chain rack nor a
fixed vocal strip substitutes for session-wide interaction.

**Criticism:** static role-based gain/EQ/dynamics planning has a limited view of
the session. DAW faders, buses, sends and downstream processors are outside that
view. Runtime analysis/proposal history is not serialized. The system is not
sample-synchronous group automation and does not establish perceptual masking,
reference matching or integrated-loudness compliance. A larger DSP menu would
not solve these weaknesses.

**Upgrade:** invest first in trustworthy session membership, bus/double-counting
warnings, explaining each proposal, better stale-analysis handling and recall
of useful history. Test real multitracks, level-matched results and measured
multi-instance limits. Consider section-aware analysis and reference workflows
only after baseline proposals are consistently useful. Reuse shared DSP without
turning Mix into a second visually independent Modular platform.

**Decision:** keep distinct and center the assistant, not the rack, in its product
story. [Current architecture and boundaries](../plugins/mix/README.md).

### Stack

**Keep:** four layers, independent placement, key ranges, transpose, pan and Chop
alongside Rhythm Genie/Granulator. These are real capabilities Modular Instrument
does not yet replace. [Stack source](../plugins/stack/src/PluginProcessor.cpp) and
[instrument rack](../core/include/sattari_audio_core/SattariInstrumentRack.h).

**Criticism:** another instrument container divides media handling, UI and recall
work. Several factory setups depend on the user supplying samples, so they are
arrangements rather than a finished sound library.

**Upgrade:** treat Stack as the feature and compatibility reference for Modular
Instrument. Preserve it until layer mapping, source ownership, missing-media
recovery, presets and sound reproduction have explicit parity evidence.

**Decision:** maintain compatibility; avoid a separate major redesign while the
instrument direction is consolidated.

## Upgrade Sequence

The following order is proposed. It is intentionally separate from the public
release gates in the [release plan](release-plan-2026-09-30.md).

| Stage | Deliverable | Acceptance before moving on |
| --- | --- | --- |
| 1. Trust and baseline | Reproduction tests for Royal presets, zero/bypass behavior and mono/stereo width; Modular automation, latency and resource baselines | Source-confirmed issues reproduced; expectations agreed; old-session fixtures captured before changes |
| 2. Shared module contract | One registry for stable IDs, parameters, factory defaults, state versions and module UI metadata; simple and detailed views | Every exposed control reaches DSP; identifiers remain stable; saved values and automation survive supported upgrades |
| 3. One complete integration | Port Double into Modular as the first Vocal collection module | Two independent instances; full required control coverage; saved presets; macro/automation; mono/stereo; bypass, recall and audio-equivalence tests |
| 4. Distinct vocal product | Royal preset unification, explicit stage bypass, gain-matched comparison and honest widening/tuning behavior | Host/editor preset parity; bypass timing tests; real-vocal owner listening approval; no unapproved change to legacy presets |
| 5. Curated collections | Remaining Vocal modules, then Create modules, with task-based preset banks | Parameter parity or explicit documented omissions; old preset import report; musical listening, feedback/tail and host tests per module |
| 6. Instrument consolidation | Bring Stack's needed layer/source workflows into Modular Instrument | Existing Stack sessions remain supported; tested sample portability, key mapping and sound reproduction before any retirement decision |
| 7. Better mixing decisions | Mix group reliability, explainable proposals, useful recall/history and multitrack evaluation | Measured host load, session isolation, undo/recall correctness and owner-approved level-matched comparisons |

Stages 4 and 7 can be independent workstreams once their tests and scope are
agreed; neither needs to wait for every collection module. The first coding
increment should be the Royal preset consistency fix plus the Modular baseline
tests, not a sweeping engine rewrite.

## Architecture And Compatibility Rules

- Share DSP and metadata where behavior genuinely matches; keep specialized product workflows. Do not enforce one identical UI everywhere.
- Do not blindly replace Royal Chain stages with similarly named suite modules. Establish defaults, timing, level and sound equivalence first.
- Preserve current plugin IDs, host parameter IDs/indices and old-state readers. Use explicit versioned migrations and retain legacy processing where required.
- Treat a preset converter and DAW automation migration as separate problems. Report unsupported imported parameters instead of silently discarding them.
- Do not silently remap existing slot automation to a new module-identity system. Any new automation model needs a documented compatibility path.
- Avoid loading every expanded module in every slot by default without measurement. If changing preparation strategy, stage work off the audio thread and verify lifetime/thread safety.
- Keep advanced routing, third-party hosting, unrestricted module counts and automatic mastering out of this upgrade's initial scope.

## Evidence And Open Decisions

This review read current processor/editor code, module mappings, routing, presets
and documentation. It did not rebuild plugins, run fresh audio contracts, inspect
every current UI screenshot, perform DAW listening or compare competitors. Sonic
quality and customer willingness to buy overlapping products remain unmeasured.
No plugin code was changed; only this review document was added.

Before implementation, agree on three decisions: whether Vocal/Create become
collections-first; the minimum instrument workflow needed to consolidate Stack;
and how much automation/old-session compatibility must be retained in a future
engine version. Recommendation: collections-first, preserve existing sessions,
and make consolidation depend on proven parity rather than a fixed removal date.
