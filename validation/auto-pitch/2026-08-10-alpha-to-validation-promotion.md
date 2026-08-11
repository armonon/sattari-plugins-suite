---
type: lifecycle-promotion-record
product: auto-pitch
from: alpha
to: validation
date: 2026-08-10
candidate_revision: 7fef5442  (frozen; see "Frozen candidate" below)
truth_label: internal validation only; not sale-ready, not release-candidate
---

# Sattari Auto Pitch — alpha → validation

This record promotes Auto Pitch from `alpha` to `validation`. It does **not**
claim the product is finished, shippable, or equal to any commercial reference.
It claims one thing: enough objective evidence has accumulated to graduate from
alpha and enter formal validation.

Everything below is evidence that exists in this repository on this revision.
The caveats are part of the record, not a footnote to it.

## Frozen candidate

    revision  7fef5442  "Auto Pitch bench: measure CPU time, and sweep sample rate x buffer"
    DSP as of 8c2ce30   "AutoKey: stop publishing unconfident keys to the session bus"

Feature development on Auto Pitch is **frozen** for the duration of RC
qualification. Changes are permitted only where a qualification failure requires
them. The point of a candidate is that it stays still long enough to be
disproved.

## Criteria for alpha → validation

The lifecycle requires: meaningful automated coverage, DSP/behaviour validation,
regression evidence, plugin validation, and documented known limitations.

### 1. Meaningful automated coverage — MET

- `AutoPitchCoreContract` — five sound styles, modal scales, reference detune,
  MIDI targets, YIN noise robustness, graph object editing, graph time latency,
  factory workflows, UI render for Basic/Pro/Edit, and the AutoKey confidence
  gate. **PASS** on the candidate revision.
- `--autokey` — 48 cases (12 roots x major/minor x sine/harmonic-rich).
  **24/24 and 24/24.** Was 14 cases covering only white-key roots with pure
  sines until 2026-08-10.
- `AutoPitchBench` — correction, latency honesty, THD/alias proxy, CPU,
  transitions, vibrato, polyphonic stress, output safety.
- Trace contract over the public parameter surface.

### 2. DSP / behaviour validation — MET

    correction target math     mean 0.01 cents, max 0.04 cents
    latency honesty            2048 reported, 2080 measured, 0.67 ms delta
    steady-sine THD            0.0000%
    transitions                monotonic, 5.3 ms block spread
    vibrato retention          82%
    polyphonic agreement       96-98%
    output safety              83 renders, peak 0.9955, all finite

### 3. Regression evidence — MET

- Real-vocal renders on three cleared clips, with independently measured output
  safety: length preserved, all samples finite, peak <= 0.995, DC ~1e-5.
- Correction on a real sung vocal corroborated by two independent measurements:
  renderer 23.4 -> 12.8 -> 10.0 ct (45% / 57%); independent re-measurement
  20.6 -> 10.6 -> 9.4 ct (48% / 55%). Agreement within three points.
- The AutoKey confidence gate is guarded in **both** directions by contract:
  a clean tonal melody must clear it (0.327) and white noise must not (0.000).

### 4. Plugin validation — MET

    auval -v aufx Ap01 Satr    AU VALIDATION SUCCEEDED
    AU + VST3 + Standalone     Release build clean

### 5. Known limitations documented — MET

Documented, and material. See the next section.

## Known limitations carried into validation

**Operating envelope is not universal.** Measured 2026-08-10 across
44.1/48/96 kHz x 64/128/256/512/1024, Studio lock, CPU p95 against block budget:

    48 kHz  >= 128 samples     PASS   (2.3x headroom or better)
    44.1 kHz >= 128 samples    PASS
    64 samples, any rate       WARN   (~1.2x headroom)
    96 kHz, 256-1024 samples   WARN   (~1.5-1.7x)
    96 kHz, <= 128 samples     FAIL   (0.4-0.8x — over budget)

Per-block cost is nearly flat from 64 to 256 samples (1.15-1.23 ms), so a fixed
~1.2 ms per-block overhead dominates at small buffers while the 96 kHz budget
halves. **Auto Pitch must not be described as universally low-latency.** The
supported envelope is 44.1/48 kHz at 128 samples or above.

**The 2026-08-09 p95 warnings were measurement noise, not a stall.** Recorded as
19.873 and 18.631 ms; three identical idle runs then measured 11.01 / 2.74 /
2.52 ms, and a deliberately loaded run measured 8.47 ms — lower than the worst
idle run. The bench timed with `steady_clock`, so it could not distinguish DSP
cost from OS preemption. Now measured with `CLOCK_THREAD_CPUTIME_ID`; spread
fell from 4.4x to 1.12x. Any pre-2026-08-10 CPU figure should be disregarded.

**Cents-to-scale is not a valid metric for all material.** On rap it carries no
information: 50 cents is the maximum possible distance to a scale tone and
non-tonal delivery yields ~50 by construction. The corpus needs per-style
metrics, which do not yet exist.

**AutoKey cannot determine key from a solo vocal.** Not a defect — the evidence
is absent. Measured on a confirmed G# major clip, the discriminating tones sat
below the uniform baseline. AutoKey now withholds rather than publishing a guess
(gate 0.10, derived from a measured separation), but a key-following session on
a solo vocal will correctly receive no key.

**No human has listened to anything.** LISTENING NOT VERIFIED.

**No DAW has been tested.** No host matrix has been run.

## What this promotion does NOT authorise

- No claim of parity with any commercial pitch corrector
- No release-candidate status
- No shipping, signing, or distribution
- No "production ready", "validated" or "release ready" language

## Next gates

    GATE 1  performance      COMPLETE — no stall; envelope documented above
    GATE 2  corpus           BLOCKED  — 3/5 clips; missing belting, rnb,
                                        vibrato, confirmed male/female
    GATE 3  listening        NOT STARTED — the decisive gate
    GATE 4  hosts            NOT STARTED — Logic, Ableton, REAPER
    GATE 5  RC review        BLOCKED on 2, 3, 4

## Status

    alpha -> validation      PASS
    validation -> RC         BLOCKED
