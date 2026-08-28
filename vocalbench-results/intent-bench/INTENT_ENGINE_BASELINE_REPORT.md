# Intent Engine Baseline Report

**Production baseline:** `6c022cb`
**Cases:** 102 total (30 base + 72 adversarial)
**Voiced frames analyzed:** 17,673

---

## Phase 6 — Candidate Gate

| Check | Result | Status |
|-------|--------|--------|
| Shadow accuracy ≥ production | 88.8% vs 88.9% | ❌ |
| False-target rate | 6.5% | ✅ |
| Octave errors | 0 | ✅ |
| Chatter rate | 2.6% | ℹ️ |

**RECOMMENDATION: HOLD — shadow engine needs refinement before audio activation**

---

## Phase 2 — Intent Failure Distribution

| Class | Frames | % |
|-------|--------|---|
| CORRECT_PRESERVE | 5,684 | 32.1% |
| CORRECT_STABLE | 9,964 | 56.4% |
| FALSE_TARGET | 1,153 | 6.5% |
| PREMATURE_TARGET | 66 | 0.4% |
| CHATTER | 468 | 2.6% |
| LATE_ACQUISITION | 187 | 1.1% |
| UNVOICED | 9 | 0.1% |
| AMBIGUOUS | 151 | 0.9% |

---

## Phase 1 — Category Results

| Category | Cases | Prod Acc | Shadow Acc | False-Target Rate |
|----------|-------|----------|------------|-------------------|
| adv_boundary | 36 | 100.0% | 100.0% | 0.0% |
| adv_slide | 18 | 67.1% | 67.1% | 32.9% |
| adv_vib_boundary | 18 | 93.0% | 93.0% | 0.1% |
| boundary | 6 | 100.0% | 100.0% | 0.0% |
| chromatic_passing | 1 | 66.1% | 61.6% | 0.0% |
| chromatic_run | 1 | 30.2% | 20.6% | 0.0% |
| fall | 2 | 100.0% | 100.0% | 0.0% |
| repeated_note | 2 | 100.0% | 100.0% | 0.0% |
| run | 3 | 18.0% | 16.7% | 0.0% |
| scoop | 3 | 100.0% | 100.0% | 0.0% |
| semitone_slide | 3 | 63.5% | 63.5% | 36.5% |
| short_note | 4 | 100.0% | 100.0% | 0.0% |
| vibrato_boundary | 3 | 92.3% | 92.3% | 0.1% |
| whole_tone_slide | 2 | 0.0% | 0.0% | 32.9% |

---

## Phase 3 — Shadow Intent Engine Architecture

The Phase 1 prototype uses three heuristics:

1. **Slide extrapolation:** when `pitchVelocity > 0.8 MIDI/sec`, project pitch
   direction 150ms ahead and snap to the nearest scale note.
2. **Stability anchor:** when `stabilityDuration > 80ms` and `confidence > 0.55`,
   commit to the current nearest scale note with high confidence.
3. **Preservation:** when drifting from a stable note with low velocity, hold the
   previous stable note rather than chasing small pitch deviations.

**Known weaknesses in Phase 1 prototype:**
- No register awareness (octave confusion on low-confidence frames)
- Slide extrapolation tuned only for ±1-2 semitone intervals
- No chromatic passing-tone suppression
- No key-change handling

---

## Phase 5 — Adversarial Search Summary

Adversarial cases generated around: boundary, vibrato_boundary, semitone_slide.
Shadow improvement distribution: min=-9.5% median=0.0% max=0.0%

---

## Recommended Architecture (Phase 3→Production Path)

1. **Input features:** detectedMidi, pitchVelocity, stabilityDuration,
   prevStableMidi, pitchDirection, candidateDistance, confidence, key/scale,
   targetHistory (last 5), Strategy G state (hysteresisAge).
2. **Core logic:** three-tier decision: SLIDE → STABLE → PRESERVE.
   Confidence threshold gates aggressiveness.
3. **Shadow mode first** (Phases 3-4): compare against production per frame.
   Gate: shadow accuracy ≥ prod on 533+ regression cases, 0 octave regressions.
4. **Active audio only after gate passes.**

**Strategy G correction dynamics: FROZEN. Do not touch.**