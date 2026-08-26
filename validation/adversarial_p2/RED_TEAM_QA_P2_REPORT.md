# Red Team QA — Phase 2 Report

**Tests:** 24  **Pass:** 23  **Fail:** 0
**P0 failures:** 0

## Results

| Test | Severity | Status | Detail |
|------|----------|--------|--------|
| ✅ A1: silence low-latency block=32 | P0 | PASS | rc=0 voiced=0 |
| ✅ A2: DC offset low-latency | P0 | PASS | rc=0 finite=ok |
| ✅ A3: clipped signal low-latency block=32 | P0 | PASS | rc=0 ok |
| ✅ A4: rapid succession low-latency | P1 | PASS | rc=0 ok |
| ✅ A5: white noise low-latency | P0 | PASS | rc=0 |
| ✅ A6: 10s signal low-latency | P1 | PASS | rc=0 ok |
| ✅ B1: silence input harmony | P0 | PASS | crashes=0 nan=0 |
| ✅ B2: DC offset harmony | P0 | PASS | crashes=0 |
| ✅ B3: all-unvoiced trace harmony | P0 | PASS | voiced=0 crashes=0 |
| ✅ B4: 4-voice stress test | P1 | PASS | crashes=0 render_rate=2.00 |
| ✅ B5: +octave harmony voice | P1 | PASS | crashes=0 |
| ✅ B6: clipped input harmony | P0 | PASS | crashes=0 |
| ✅ C1: silence → 0 detections | P0 | PASS | detected=0 notes |
| ✅ C2: white noise → ≤1 FP | P0 | PASS | detected=0 notes |
| ✅ C3: 5-note dense chord | P1 | PASS | tp=5/5 detected=[64, 67, 74, 71, 60] |
| ✅ C4: 7-note chord → max_voices cap | P0 | PASS | detected=6 (cap=6) |
| ⚠ C5: E2 (82Hz) solo detection | P1 | WARN | detected=[52, 59] gt=[40] |
| ✅ C6: B5 (988Hz) solo detection | P1 | PASS | detected=[83] gt=[83] |
| ✅ C7: very short signal (4096 samples) | P0 | PASS | detected=3 |
| ✅ C8: chromatic cluster C4 C#4 D4 (1-semitone gaps) | P1 | PASS | tp=3/3 detected=[62, 60, 61, 73] (hard case: 1-semitone gaps) |
| ✅ X1: LivePitch → HarmonyAudio chain | P1 | PASS | crashes=0 vf=1033 |
| ✅ X2: LivePitch block=32 A4 NaN/Inf check | P0 | PASS | ok |
| ✅ X3: amount=0 passthrough | P1 | PASS | ok |
| ✅ X4: amount=1.0 speed=0.0 stress | P1 | PASS | ok |

## Gate
```
ADVERSARIAL QA PHASE 2 PASS
```