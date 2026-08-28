# Auto Pitch — Expansion Phase 2 Status Ledger
**Started:** 2026-08-25  
**Closed:** 2026-08-26  
**Frozen baseline:** b939124 (engine) / 981faac (product RC) / 9d8b096 (Phase 1 complete)

---

## Agents

| Agent | Branch | Commit | Status | Gate |
|-------|--------|--------|--------|------|
| A — Live Pitch Real Audio | feature/live-pitch | a23e6be | ✅ COMPLETE | LIVE PITCH PRODUCTION PASS |
| B — Harmony Audio Engine | feature/harmony-engine | 021c639 | ✅ COMPLETE | HARMONY AUDIO RC |
| C — Poly Tune Phase B | experiment/poly-tune | 594b8a4 | ✅ COMPLETE | POLY TUNE BETA |
| D — Red Team Integration | validation/adversarial-expansion | 48a2449 | ✅ COMPLETE | ADVERSARIAL QA PHASE 2 PASS |

---

## Gates

| Gate | Status | Details |
|------|--------|---------|
| LIVE PITCH PRODUCTION PASS | ✅ PASS | Live 10.8ms @ block=128/44100Hz ≤20ms; 12 voices × 2 modes: 0 crashes, 0 NaN |
| HARMONY AUDIO RC | ✅ PASS | 14/14 vocal cases, 60/60 corpus clips, 0 crashes, avg VL=16.7¢ |
| POLY TUNE BETA | ✅ PASS | Precision=1.000 Recall=0.988 (was 0.699), Chord Acc=96.8% |
| ADVERSARIAL QA PHASE 2 PASS | ✅ PASS | 23/24: A(6/6) B(6/6) C(7/8) X(4/4) — 0 P0 failures |
| VOCAL BENCH v2 (regression) | ✅ PASS | 39/39, mean=8.194¢, oct=0, crash=0 |
| INTENT BENCH v3 (regression) | ✅ PASS | GO — SLIDE_DESTINATION_V2 active |
| HARMONY BENCH (regression) | ✅ PASS | 14/14, render_rate=300%, VL=16.7¢ |
| POLY BENCH A (regression) | ✅ PASS | Precision=0.951, Recall=0.699 (baseline intact) |
| POLY BENCH B (regression) | ✅ PASS | Precision=1.000, Recall=0.988 |
| EXPANSION PHASE 2 COMPLETE | ✅ PASS | See declaration below |

---

## Phase 2 Results vs Goals

| Agent | Target | Result |
|-------|--------|--------|
| A: Live latency | ≤30ms (prefer ≤20ms) | **10.8ms** ✅ |
| A: Quality | 0 crashes, 0 NaN | **0/0** ✅ |
| B: Corpus render | 0 crashes, render≥85% | **0 crashes, 300%** ✅ |
| B: Voice-leading | ≤200¢ mean | **16.7¢** ✅ |
| C: Recall | ≥0.85 (was 0.699) | **0.988** ✅ |
| C: Precision | ≥0.93 | **1.000** ✅ |

---

## Phase B Algorithm Change (Agent C)

Phase A used `librosa.piptrack` with conf_threshold=0.20 on single frames.
Phase B replaces with **spectral salience** (`librosa.salience`, harmonics 1-6):
- Salience accumulates harmonic energy into the fundamental → no FP from overtones
- Temporal average over full signal for stability
- Harmonic suppression: suppress H if ratio(H/L) ≈ integer 2-5 AND sal(H) < sal(L)×0.40
- n_fft=8192 for 5.4Hz/bin resolution at low frequencies

Known limitation: E2 (82.4Hz) within 2.4Hz of min_hz=80.0 floor.
Detection unreliable for MIDI ≤ 40. Accepted risk for vocal/instrument use case.

---

## Accepted Risks
- RUN_up_350ms: 248.5¢ transient, 0 SEVERE / 120 MODERATE (inherited from Phase 1)
- Poly Tune E2 floor: MIDI 40 (82.4Hz) near min_hz=80.0; P1 severity, not vocal range

## Pending Integrator Action (same as Phase 1)
- [ ] SSH key addition to GitHub → `git push origin main`
- [ ] Manual DAW testing: Ableton / Logic / FL Studio
- [ ] Feature branch review + merge (integrator approval required per DSP constraint)

---

## Declaration

```
EXPANSION PHASE 2 COMPLETE
Date: 2026-08-26
Agents A/B/C/D: all GATE PASS
All regression suites: PASS
Red team P0 failures: 0
Frozen baseline: INTACT — Strategy G, Intent v3, CME unchanged
```

_Last updated: 2026-08-26 by Integrator (Joey)_
