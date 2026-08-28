---
type: feature-validation
product: auto-pitch
date: 2026-08-19
lifecycle: alpha
truth_label: internal validation only; nothing here is a shipping claim
status: PHASE 1 OF THE MULTI-WORKSPACE EXPANSION — machine-verified, no listening
---

# Phase 1: the shared song model and the KEY workspace

First increment of the four-workspace direction (KEY / TUNE / HARMONY / EDIT).
Scope followed the phase plan exactly: drop analysis, waveform, global key, BPM,
detune, scale-region detection, neutral section segmentation, timeline
visualization, confidence carried on every conclusion.

## The shared song model

`core/include/sattari_audio_core/SattariSongModel.h` — ONE structure every
workspace reads (`SongAnalysis`: global key + alternative, tempo + octave
ambiguity, tuning/detune, scale regions, neutral sections, display waveform).
Filled by the existing drop-analysis worker from the SAME finalised estimator
pass — the per-~15 s section verdicts the estimator already computed internally
are now consumed instead of discarded. No new analysis pass, no duplicate logic.

Region building reuses the discipline the Key Map fix validated on a real song
(167 flicker segments -> 1): neighbour-vote smoothing, run-length encoding, then
short regions absorbed into the more-agreeing neighbour. Sections are NEUTRAL
("Section A/B/C") from two defensible boundary types — scale changes and
sustained ~6 dB level shifts. Calling something a Chorus needs repetition
analysis Phase 1 does not have; the No Result philosophy applies to labels too.

Measured on real GiantSteps tracks (`AutoPitchRender --songfile`):

```
1009960: 1 region  (E major, margin 0.196)          <- single-key song: NO false modulations
1004923: 2 regions (F minor 0-90.6s, C minor after)
1017921: 3 regions (E major | B major 0.033 | E major) <- excursion carries its low margin honestly
```

## The workspaces

`KEY | TUNE | HARMONY | EDIT` navigation in the persistent header, with a
musical summary label that always names its source (`SONG ...` from the model,
else `AUTO/KEY MAP/BUS/MANUAL ...` from the live state).

- **KEY** — big key/tempo/tuning/margin summary, alternative key, the scale map
  (waveform + coloured key regions, reusing the production `KeyMapTimeline`),
  section list, and a click-to-inspect region readout that flags sub-gate
  regions as "shown, not applied". Empty state is a drop invitation; a drop
  anywhere in the plugin switches to KEY where the analysis lands.
- **TUNE** — the everyday view: key strip, main knobs, the graph as a monitor
  (capture toggles only), ADVANCED drawer.
- **EDIT** — the graph full-height with the command bar and per-object sliders.
- **HARMONY** — present, disabled, tooltip says why: chord-aware voices need the
  Phase 2 chord map first. A dead button labelled as such is more honest than a
  page of controls that do nothing.

All three live pages screenshot-verified via `--render-ui-key` / `--render-ui`
/ `--render-ui-edit` (one status-label overlap in EDIT found in the screenshot
and fixed).

## What Phase 1 deliberately does NOT do

- No chords, no modulation classification, no named sections — Phase 2.
- No Follow Song Map — Phase 3; the existing Key Map + `useKeyMap` remains the
  working per-time key follower until the model drives it.
- The KEY page's populated state is data-verified through --songfile prints and
  uses the same timeline component production keymaps render through, but a
  full drop-in-DAW pass has not been done.
- No listening evidence, no host testing; CI still billing-blocked.

## Regression

nulltest 8/8 -inf, livetest PASS, songfile precedence PASS, wetalign unchanged
(-93), smoke + core contract pass.
