---
type: gate-finding
product: auto-pitch
candidate_revision: 7fef5442
date: 2026-08-14
gate: 4
truth_label: internal validation only; nothing here is a shipping claim
status: TWO BLOCKERS FOUND BEFORE THE MANUAL MATRIX WAS RUN
---

# Auto Pitch — Gate 4 test A, measured offline

Gate 4 test A was written as a manual DAW procedure: two sample-aligned copies of
a vocal, Auto Pitch on one with correction off, invert, **pass is below
-60 dBFS**. Repeated by hand in three hosts.

That test asks one question — *does a pass-through come back sample-aligned with
what went in?* — and it does not need a DAW to answer. `AutoPitchRender
--nulltest` now answers it offline.

**The plugin was not modified.** Auto Pitch remains frozen at `7fef5442`; only
the test tool changed. Measuring a freeze is the point of freezing it.

```
  engine / signal                              null dBFS  verdict
  correction OFF, print engine, steady             -1.0     FAIL   (reported 2048; best null -43.8 dB at +32)
  correction OFF, print engine, vibrato            -1.0     FAIL   (reported 2048; best null -43.8 dB at +32)
  Amount 0, print engine, steady                   -1.0     FAIL   (reported 2048; best null -43.8 dB at +32)
  Amount 0, print engine, vibrato 50ct             -1.0     FAIL   (reported 2048; best null -43.8 dB at +32)
  Amount 0, liveEngine=1 (LOCKED OFF)              -1.0     FAIL   (reported 2048; best null -43.8 dB at +32)
```

---

## FINDING 1 — the reported latency is 32 samples short, and it fails Gate 4

Auto Pitch reports **2048** samples. The audio comes back at **2080**. The
0.67 ms delta was already recorded in the 2026-08-09 objective benchmark and
marked `pass`; what had not been established is its consequence.

A host that delay-compensates by the reported figure places this track **0.67 ms
early against the rest of the session, permanently**. Gate 4 test A as written
therefore cannot pass in any host: it reads **-1.0 dBFS against a -60 dBFS bar**.

This is not a DSP defect. Removing the 32-sample offset takes the null to
**-43.8 dB**, so the audio itself is very nearly a pass-through — the number
being published is simply wrong. That distinction matters, because the fix is a
latency report, not an engine.

**Not fixed here.** Changing reported latency changes host-visible behaviour and
would invalidate the frozen candidate along with every measurement taken against
it. It belongs in the RC decision, not in a measurement pass.

### The residual that is left over

At perfect alignment the null is -43.8 dB, not the -60 dB the gate asks for.
0.6% of the signal survives inversion and **this has not been isolated** — a
level trim, the final soft ceiling, and Rubber Band's own reconstruction are all
plausible and none has been ruled out. Recorded as open rather than guessed at.

---

## FINDING 2 — `liveEngine` and `lowLatency` are host-visible controls that do nothing

`PluginProcessor.cpp:3187`:

```cpp
// Quality lock. Legacy sessions still deserialize Live/Low-Latency values,
// but the current PSOLA path failed real-vocal validation (worse pitch error
// plus grain). Keep the parameters for compatibility while always rendering
// monophonic correction through the proven print engine.
const bool useLiveEngine = false;
```

The decision is sound and it is documented. The problem is what remains exposed:

- `liveEngine` and `lowLatency` are still registered parameters. They appear in
  every host's automation list, they can be automated, and they change nothing.
- The Gate 4 automation checklist lists both among the 21 parameters to verify.
  A tester would confirm they automate correctly and learn nothing.
- The recorded latency tiers — **Rubber Band 42.7 ms / Live-PSOLA 28.6 ms /
  Low-Latency 15.4 ms** — describe code that cannot execute. The shipping build
  has **one** tier. Anything claiming low-latency monitoring is claiming a mode
  that is compiled out at a hard-coded `false`.

The five rows above are identical to 0.1 dB *because there is only one engine*.
That identity is now asserted by the test: if the lock is ever lifted, the
`liveEngine=1` row diverges from its twin and the difference is the evidence.

---

## What the PSOLA measurement found, for whenever the lock is revisited

The quality lock cites "worse pitch error plus grain" without a mechanism. There
is one, and it is measurable (`PsolaShifter` driven directly, unity ratio):

| condition | null |
|---|---:|
| constant period, correct | **-149.9 dB** |
| constant period, deliberately wrong (150 Hz on a 220 Hz voice) | **-152.4 dB** |
| pure sibilance, fabricated period | **-152.7 dB** |
| period moving — 10 cents of vibrato | **+1.4 dB** |
| period moving — 50 cents of vibrato | **+1.3 dB** |

The shifter is exact while the period holds still — **even when the period is
wrong**, which disproves the obvious guess that the fabricated 150 Hz fallback at
`PluginProcessor.cpp:3371` smears consonants. It does not.

What breaks it is the period *moving*. Grains are placed at analysis mark
`a = round(s/P)*P` while the synthesis accumulator steps by `P`; when `P`
changes, `a - s` changes with it and the lattice slides under the signal by up to
half a period (**3.73 ms peak-to-peak measured**, bounded by ±P/2 as the
mechanism predicts). Ten cents of vibrato is enough. No real voice holds a period
still, so the path was effectively never operating in its exact regime.

Two claims here were wrong on the way and are corrected in place: the consonant
hypothesis (disproved above), and a first wander figure of 15.19 ms that was
correlation aliasing on a periodic signal — a lag search wider than half a period
locks onto the wrong peak. Re-measured with a broadband probe: 3.73 ms.

---

## Gate 4 status

- [x] Test A automated — `AutoPitchRender --nulltest`
- [ ] **BLOCKER**: latency reported 32 samples short; test A cannot pass as written
- [ ] **BLOCKER**: two dead host-visible parameters, and latency-tier claims that
      describe unreachable code
- [ ] Residual -43.8 dB at perfect alignment not isolated
- [ ] Manual matrix (Logic / Ableton / REAPER) — unchanged, still required for
      scan, state, automation, transport, render, bypass, editor, multi-instance

Test A should not be attempted by hand until the latency report is settled.
Running it now costs three DAW sessions to rediscover a number this file already
has.
