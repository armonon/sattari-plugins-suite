---
type: rc-qualification-campaign
product: brain (Sattari Compass)
date: 2026-08-11
current_lifecycle: alpha
target_lifecycle: validation -> release-candidate
truth_label: internal validation only; nothing here is a shipping claim
status: GATE 0 BLOCKED
---

# Sattari Compass — RC qualification campaign

Same discipline as the Auto Pitch campaign, **different gates**, because Compass
is not an audio processor. It emits no processed audio, so there is nothing to
listen to. Its equivalent of sound quality is **analysis accuracy against ground
truth**, and its equivalent of an artifact is **a confident wrong answer**.

Copying Auto Pitch's gate list unchanged would have produced a listening gate
with nothing to hear and no accuracy gate at all.

---

## GATE 0 — lifecycle: **BLOCKED**

Compass does **not** qualify for `alpha -> validation` today, and the reason is
not a missing document.

### The authority runs the weakest detector in its own product family

Measured on 500 labelled GiantSteps tracks (`SattariKeyDatasetEval`, 2026-08-11):

**CORRECTED 2026-08-11.** The first version of this table named
Krumhansl-Schmuckler as Compass's profile. That was wrong. `SATTARI_KEY_PROFILE`
defaults to **2**, which falls through the `#if` chain to the `#else` branch —
**Aarden-Essen**, not Krumhansl. I read the header's prose instead of its
`#define`. Compass is therefore WORSE than first reported, and the profile
question is more interesting than "take the free win".

Measured on TWO corpora, because one is not enough — GiantSteps is EDM-only and
the header already warns that Sha'ath is EDM-biased:

| profile | GiantSteps 500 (EDM) MIREX / exact | GTZAN 837 (9 genres) MIREX / exact |
|---|---:|---:|
| **Aarden-Essen — what Compass runs** | **47.34 / 39.8%** | **46.30 / 38.8%** |
| Krumhansl-Schmuckler | 51.90 / 38.8% | 52.22 / 41.6% |
| Temperley/Kostka-Payne | 48.86 / 39.0% | 49.81 / 43.2% |
| **Sha'ath (audio)** | **56.56 / 45.2%** | **53.36 / 43.6%** |
| Bellman-Budge | 44.84 / 37.6% | 47.30 / 41.8% |
| `keynet_spec.onnx` (Sattari Key) | — | 72.1 claimed, **unverified here** |

Sha'ath wins BOTH corpora, on BOTH metrics. Its GiantSteps lead (+9.22 MIREX
over Aarden) does shrink on GTZAN (+7.06), which is the EDM bias showing up
exactly as documented — but it does not reverse, and it still leads on raw exact
key (43.6% vs 38.8%), which is the metric the header argues matters most for a
corrector because "a perfect-fifth error tunes to the wrong scale".

**Aarden-Essen is last or near-last on both corpora.** The header's reasoning for
choosing it ("best-exact literature profile at 45% vs 41% Sha'ath") was measured
on **80 GiantSteps tracks**. On 500 GiantSteps tracks and 837 GTZAN tracks that
ordering does not hold.

Verified in source, not inferred:

- `plugins/brain/src/BrainProcessor.cpp` contains **zero** references to
  `KeyNet`, `keynet`, or `onnx`. It builds an HPCP chroma and calls
  `sattari::dsp::detectKey(frame)` at line 211.
- `plugins/song-key/src/SongKeyProcessor.cpp` loads `keynet_spec.onnx` at
  line 77-79.
- `keynet_spec.onnx` is installed on this machine.
- `SattariKeyNetSpec.h` records 72.1 vs 61.5 MIREX on GiantSteps+GTZAN.

And from Compass's own file header:

> Sattari Key is the focused detector and lower-priority fallback publisher.
> Compass is the conductor.

**The conductor is 20 MIREX points less accurate than the fallback it outranks.**
Every plugin that follows session key — Auto Pitch, Harmony, Maqam, Scaler —
receives Compass's answer in preference to Sattari Key's better one.

This is a correctness defect in the product whose entire purpose is to be right
about the session, so it blocks promotion rather than being carried as a known
limitation.

### What Compass already gets right

Worth recording, because it is the model the rest of the suite should follow.
Compass gates its bus publication on confidence **and** requires stability
across analyses before committing:

```cpp
if (conf >= kMinPublishConfidence)   // and kStableAnalyses hysteresis
```

> Compass stays silent until a confident key commits; publishing an invented
> C major would corrupt the suite.

That is exactly the defect that had to be fixed in Auto Pitch on 2026-08-10
(`8c2ce30`). Compass had it right first. The bus-publisher audit now stands at:

| publisher | publishes | gated |
|---|---|---|
| brain (Compass) | a DETECTED key | yes — confidence gate + stability hysteresis |
| song-key | a DETECTED key | yes — holds until a confident commit |
| auto-pitch | a DETECTED key | yes — confidence gate added 2026-08-10 |
| granulator | a USER-CHOSEN key | n/a — nothing is estimated |

**Correction, 2026-08-11.** An earlier note in this campaign listed Granulator as
"the one ungated publisher". That was wrong, and the error is worth recording
because of how it was made: it came from `grep -c confidence` returning 0, not
from reading the code. Absence of the word was treated as absence of the gate.

Granulator does not detect anything. It publishes `sharedKey.keyRoot` — the key
the USER set on the plugin — and only when:

- the `broadcastKey` parameter is on, which **defaults OFF**, and
- a note is currently sounding (`noteThisBlock || voiceLive`)

There is no estimate, so there is no confidence to gate. An opt-in broadcast of a
key the user typed is a decision, not a guess.

### The real observation: three publishers, three policies on user-chosen keys

Not a defect, but the suite has no single stated rule about who may occupy the
one bus slot with a key the user chose rather than one that was detected:

- **Auto Pitch REFUSES.** Its publisher comments: *"A manually-selected key is a
  local choice, not session-wide truth — publishing it would stomp Sattari Key
  (or another detector) on the single bus slot. So: detector -> source, manual
  -> passive."*
- **Compass PUBLISHES** manual overrides deliberately (`brainManual` ->
  `brainManualRoot`), on the reasoning that the conductor's user is setting
  session truth.
- **Granulator PUBLISHES**, opt-in and off by default, while a note sounds.

All three are individually defensible. Together they mean the answer to "whose
key wins when a user has typed one" depends on which plugin they typed it into.
That is a coherence question for the Compass contract gate, not a bug to fix in
Granulator.

### To unblock Gate 0

- [ ] Decide the detector question (options below)
- [ ] Re-measure Compass's actual path on GiantSteps after the change
- [ ] Freeze a candidate revision
- [ ] Write the promotion record with the measured number

Three options, cheapest first:

1. **Change the profile constant** from 2 (Aarden) to 3 (Sha'ath): **+9.22
   MIREX on GiantSteps, +7.06 on GTZAN, and +4.8 points of raw exact key on
   GTZAN**, for a one-line change with no new dependency.

   **BUT THIS CANNOT BE DONE UNILATERALLY TODAY.** `SATTARI_KEY_PROFILE` is a
   GLOBAL compile-time `#define` in a shared header, so changing it changes the
   detector for every consumer — including Auto Pitch's AutoKey fallback, which
   is FROZEN at `7fef544a6d49` for RC qualification. Changing it would silently
   invalidate the frozen candidate and every measurement taken against it.

   Options: scope the profile per-consumer (which the shared
   `KeyAnalysisEngine` direction would give for free, since a config object
   replaces a global define), or defer until the Auto Pitch RC decision is made.
   Deferring is cheap; breaking a freeze is not.
2. **Wire in `keynet_spec.onnx`**, as Sattari Key already does: +20.2 MIREX, at
   the cost of a model dependency in the session conductor and a decision about
   what happens when the model is absent.
3. **Make Compass defer to Sattari Key** when Sattari Key is present and
   confident, rather than outranking it. Architectural, and arguably the most
   honest: the focused detector should win on its own subject.

Not chosen here. This is a qualification finding, not a licence to start
rewriting the session conductor.

---

## GATE 1 — accuracy (replaces "listening")

Compass publishes seven things. Only key has a measured accuracy today.

| field | ground truth available | measured |
|---|---|---|
| key / mode | GiantSteps 500, GTZAN 837 | **47.34 / 46.30 MIREX** |
| confidence | — | **not validated** |
| chord root / quality | none on disk | no |
| tempo | host-provided, not detected | n/a |
| time signature | user-set | n/a |
| section / energy | none | no |
| reference Hz (detune) | synthetic possible | no |

- [ ] Key accuracy re-measured on the shipping path
- [ ] Cross-genre check: GTZAN and FMA, not GiantSteps alone (GiantSteps is EDM
      and flatters an EDM-tuned detector)
- [ ] **Confidence must have semantics.** Bucket predictions by reported
      confidence and show accuracy rises with it. A confidence that does not
      correlate with correctness is worse than none, because the whole suite
      gates on it.
- [ ] Chord detection: either build a labelled set or document that chord output
      is unvalidated and mark it as such in the UI
- [ ] Energy / section: same — validate or declare

## GATE 2 — Compass contract behaviour

The skill's Compass rule: *may enhance, must not create fragile basic
functionality.* Followers must behave sanely with no context.

- [ ] Context present / absent / low-confidence / stale
- [ ] Context changes mid-session; follower tracks without glitching
- [ ] User override beats detection
- [ ] Host override beats detection
- [ ] Session restore reproduces the published state
- [ ] Two Compass instances: exactly one publishes, no fighting over the slot
- [ ] Compass absent entirely: every follower still works standalone
- [ ] Bus schema version mismatch handled (SessionMessage is schemaVersion 3)

## GATE 3 — performance

Compass runs FFT and detection on a worker thread, so the audio-thread cost
should be near zero. That is a claim to verify, not assume.

- [ ] Audio-thread cost measured with `CLOCK_THREAD_CPUTIME_ID`, not wall time
      (wall time gave a 4.4x run-to-run spread on Auto Pitch and produced two
      false warnings)
- [ ] Worker-thread cost and analysis cadence
- [ ] Sample rate x buffer matrix
- [ ] 8 and 16 instances
- [ ] Bus file write rate under multiple instances — it is one shared file

## GATE 4 — hosts

Reuse the Auto Pitch matrix structure, plus the bus-specific cases:

- [ ] Logic, Ableton, REAPER; scan, state, automation, bounce, multi-instance
- [ ] **Compass + a follower in one session**, key propagates
- [ ] Compass removed mid-session: follower degrades gracefully
- [ ] Two DAWs open at once — the bus is a file at
      `~/Library/Sattari Audio/`, not per-process

## GATE 5 — RC review

    Accuracy            PASS / FAIL
    Confidence semantics PASS / FAIL
    Compass contract    PASS / FAIL
    Performance         PASS / FAIL
    AU/VST3 validation  PASS / FAIL
    Host compatibility  PASS / FAIL
    State/reload        PASS / FAIL
    Known blockers      ____

---

## Status

    GATE 0  lifecycle    BLOCKED — the conductor runs the weakest detector
    GATE 1  accuracy     baseline measured on 2 corpora (47.34 / 46.30 MIREX)
    GATE 2  contract     NOT STARTED
    GATE 3  performance  NOT STARTED
    GATE 4  hosts        NOT STARTED
    GATE 5  RC review    BLOCKED

Compass stays at `alpha`. Promotion needs the detector question answered first.
