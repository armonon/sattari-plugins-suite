---
type: assessment
category: sattari
area: audio-plugins
status: active
updated: 2026-08-09
---

# Plugin readiness ranking

Ranked by how usable and close-to-complete each plugin is *today* — i.e. how short the path
is to a DAW-validated, sellable product. All 8 share the core, the packaging path, the modern
LookAndFeel, and the preset system; the differences below are about DSP completeness, surface
area, and quality-bar risk. Nothing here is DAW-listening-validated yet (that gate is the same
for all).

## Tier 1 — closest to shippable (simple, single-purpose, feature-complete)

1. **Sub-Conjurer** — sub-bass enhancer. Real synth+envelope+limiter DSP, fully wired, and just
   upgraded (low-frequency tracking ring + sample-rate-correct limiter). Small surface, one
   clear job. *To ship:* DAW smoke + a real-material listening pass on low end.
2. **Side-Chain Master** — sidechain compressor/ducker. Correct gain-computer math, proper
   external sidechain bus, atomic gain-reduction meter. Feature-complete. *To ship:* listening
   pass + confirm sidechain routing across Logic/Ableton/FL.
3. **Sattari-Arp** — MIDI arpeggiator. Sample-accurate event splitting, realtime allocations
   fixed, factory presets. MIDI effects are the easiest class to validate (no audio quality
   bar). *To ship:* DAW MIDI-routing smoke; essentially close.
4. **VoxKey** — voice→MIDI. Real, textbook YIN detector with a functional contract test
   (440 Hz → A4). Complete melody/harmony/bass modes. *To ship:* real-vocal tracking
   validation (latency, false notes), which is the one quality risk.

## Tier 2 — complete but higher quality-bar or newer

5. **BUS** — bus/aux channel strip (newest). Feature-complete: trim, HPF/LPF, 3-band tone, glue
   comp, **now-anti-aliased 4x oversampled drive** (latency-reported), width, pan, parallel mix,
   ceiling, modes. *To ship:* it's the least battle-tested (brand new); fixed EQ band
   frequencies and 6 dB/oct filters are first-pass; listening pass needed.
6. **Auto-Pitch** — flagship-quality autotune. Realtime RubberBand engine, scale quantize,
   auto-key, styles, trace-import; audio-thread allocations fixed. *To ship:* (a) the
   **RubberBand commercial-licence decision** gates a paid release, and (b) vocal correction
   quality needs a real-corpus listening pass. Bigger surface = more to validate.
7. **Royal-Chain** — modular vocal chain (clean/tune/control/tone/de-ess/colour/width/space/
   finish). Architecturally complete, but it's a *chain of many DSP modules*, so it carries the
   highest "does the whole thing sound good end-to-end" risk in this tier. *To ship:* per-module
   voicing + full-chain listening validation.

## Tier 3 — long road (most ambitious, least validated)

8. **StemDeck** — 4-deck DJ/stem performance and arrangement app. It now has
   automatic beat/downbeat analysis, variable-tempo warp sync, optional neural
   beat/key/stem engines, stable plugin automation capture, sample-identical replay,
   and a functional Build workspace. It remains Tier 3 because its risk moved from
   missing architecture to certification breadth: real-music accuracy, separation
   model distribution, plugin isolation, physical hardware, DAW matrices, long-session
   soak, licensing, signing, and installer validation.

## If the goal is first revenue

Ship a Tier-1 effect first (Sub-Conjurer, Side-Chain Master, or Sattari-Arp) — they're feature
complete, low-risk to validate, and prove the whole pipeline (build → validate → install →
license → sell) end to end on something small. Then **Sample Library Brain** (not yet built) is
the best *new* product because it reuses analysis primitives that already exist (VoxKey's YIN,
StemDeck's chroma/energy) with no DAW to build. Keep StemDeck as the flagship that the platform
matures toward, not the first thing you try to finish.
