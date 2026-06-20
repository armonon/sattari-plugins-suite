---
type: product-validation-report
suite: sattari-audio
truth_label: internal validation only; not sale-ready
product: PRODUCT_ID
version: VERSION
release_gate_before: prototype
release_gate_target: internal alpha
created: YYYY-MM-DD
---

# PRODUCT — validation report

## Summary

- Current gate:
- Target gate:
- Verdict: hold / promote / blocked

## Machine validation

- [ ] Unit tests
- [ ] Export tests
- [ ] Render manifest generated
- [ ] Determinism/repeatability check where applicable
- [ ] Pluginval strictness 5 for VST3 where applicable
- [ ] auval for AU where applicable
- [ ] Standalone launch smoke where applicable

## Product-specific validation

- Auto Pitch: vocal corpus before/after renders, pitch trace, formant/vibrato/artifact review.
- Rhythm Genie: MIDI validity, DAW import, General MIDI kit compatibility, groove listening.
- Pure Tone Generator Pro: band-limited DSP, limiter/mute/fade safety, output meter, safe-volume listening.
- Cymatic Generator: deterministic visual render, PNG/MP4 export, manifest, visual proof pass.

## Export artifacts

| Artifact | Path | SHA-256 | Notes |
|---|---|---|---|

## User/listening tests

Link to listening report(s):

## Known limitations

- 

## Blockers

- 

## Next actions

1. 
