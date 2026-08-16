# Sattari — session handoff

Last updated 2026-08-16 at `b4f8eb9`, branch `harmony-engine-and-portal-compass`,
PR #1. Everything below is pushed.

---

## Standing constraints — read before doing anything

1. **Do not work on StemDeck, or on signing/notarization.** User's words: *"lets
   work on everything except stem deck and otarization, keep that in mind from
   now on."* Task #66 (StemDeck auval) is parked for this reason, not forgotten.
2. **Do not ask the user for more vocals.** *"if possible dont ask for any more
   vocals from me."* The corpus is complete at 6/5 clips with full tag coverage.
3. **Audio policy.** Only owned, cleared, or public-domain material. Source audio
   lives outside git, referenced by absolute path and SHA-256.
   `public_release_allowed: false` on the user's three clips — never publish or
   upload them.
4. **The listening test is the user's task and cannot be delegated, simulated, or
   inferred.** Never write scores the user did not give.
5. **Voice-instrument work is VOICE -> MUSICAL TIMBRE -> INSTRUMENT.** Never
   PERSON -> SPEECH CLONE. Do not build anything whose purpose is making a person
   appear to say arbitrary words.
6. **CI alerting.** GitHub Actions is blocked at the account level (billing). Runs
   die in 3-4 s with `steps: 0` and no runner assigned, on both `ubuntu-latest`
   and `macos-14`. Do **not** re-report each identical event. Alert only when: the
   cause changes, a workflow actually executes and fails, a blocked workflow
   resumes, or a new affected job appears. Local verification carries everything
   for now. Only the user can fix the billing.

---

## Auto Pitch — the flagship. Frozen at `7fef5442`.

**Do not modify the plugin.** Tooling and documents are fair game; plugin DSP,
parameters, and reported latency are not. Rebuilding it mid-matrix voids every
measurement taken against it.

```
GATE 0 lifecycle     PASS       validation
GATE 1 performance   PASS       CPU p95 2.25 ms vs 10.67 ms budget, ~4.7x headroom
GATE 2 corpus        PASS       6 clips, all 9 coverage tags, CC BY where sourced
GATE 3 listening     READY      18 items — USER'S TASK, blocking
GATE 4 hosts         2 BLOCKERS found offline before the manual matrix ran
GATE 5 RC review     BLOCKED    on 3 and 4
```

### Gate 3 — the critical path

18 items, 6 sets A-F, at `~/Desktop/vocal test/listening-package/`. RMS matched to
0.001-0.105 dB, key sealed. Only the user can score these. When they do, write the
Gate 3 record from their numbers and nothing else.

### Gate 4 — two blockers, both recorded in
`validation/auto-pitch/2026-08-14-gate4-null-and-engine-lock.md`

1. **Reported latency is 32 samples short** — 2048 published, 2080 actual. Test A
   reads −1.0 dBFS against a −60 dBFS bar and **cannot pass in any host**. Remove
   the offset and it nulls at −43.8 dB, so the audio is nearly transparent and the
   published number is simply wrong. Fixing it is host-visible, so it is an **RC
   decision, not a freeze-time change**.
2. **`liveEngine` and `lowLatency` are automatable parameters that do nothing.**
   `PluginProcessor.cpp:3187` pins `useLiveEngine = false` — a documented quality
   lock added after PSOLA failed real-vocal validation. Consequence: the recorded
   **42.7 / 28.6 / 15.4 ms latency tiers describe unreachable code**; the shipping
   build has one tier. Any low-latency monitoring claim is false as built.
3. Open and un-isolated: the −43.8 dB residual at perfect alignment. A level trim,
   the final soft ceiling, and Rubber Band reconstruction are all plausible; none
   ruled out. Do not guess in the document.

**Do not run Gate 4 test A by hand until the latency report is settled** — it will
fail for a known reason and cost three DAW sessions. Run
`AutoPitchRender --nulltest` instead.

The rest of the manual matrix (scan, state, automation, transport, render, bypass,
editor, multi-instance) still needs real DAWs:
`validation/daw/2026-08-10-auto-pitch-host-matrix.md`. **Step 0 first** — stale
April `AutoPitch` bundles with a different identity (`OGTA`/`OGT_`) are installed
and will appear beside the real plugin in every host list. Confirm what they are
before deleting; they may be another product.

---

## Compass — alpha, Gate 0 blocked, and the blocker moved

The original finding (the conductor runs the weakest profile) is **superseded by a
held-out evaluation** — `validation/keynet/2026-08-12-held-out-evaluation.md`.

Every corpus in this repo is training data: `scripts/train_keynet_cqt.py` trains a
chroma teacher on GiantSteps + GTZAN, then pseudo-labels FMA. So a genuinely
held-out set was built — Schubert Winterreise (Zenodo 3968389, CC BY 3.0), 48
tracks, expert global-key annotations.

| path | GiantSteps (train) | GTZAN (train) | **SWD (HELD OUT)** |
|---|---:|---:|---:|
| chroma Aarden — what Compass runs | 47.34 | 46.30 | **88.12** |
| chroma Sha'ath — previously recommended | **56.56** | **53.36** | 80.00 |
| neural keynet_spec — the "+20 MIREX" | **87.44** | 64.50 | **55.00** |

Three conclusions, all of which reverse earlier advice in this repo:

- **The +20.2 MIREX claim has no support outside its training data.** The model is
  last of six paths held out.
- **Switching Aarden -> Sha'ath would have been a −8.12 MIREX regression.** It was
  recommended twice on contaminated evidence. Aarden ties for best.
- **Confidence calibration does not hold out either** — monotonic on training data,
  non-monotonic held out. Compass gates on exactly that number.

**Therefore: do not switch the profile. Do not wire in the model.** Gate 1 now
needs a held-out corpus with pop/rock/electronic in it; one classical set answered
only what it could. 48 tracks, one composer, one instrumentation — and genre vs
duration is confounded, because `LogSpec` pools any input to 256 frames.

`KeyAnalysisEngine` (`core/include/sattari_audio_core/SattariKeyAnalysis.h`) is the
durable win: one detector, per-consumer config instead of a global `#define`. It is
what made a six-way comparison possible under one scorer, and what stopped a
profile change from silently invalidating Auto Pitch's freeze. `KeyAnalysisConfig{}`
reproduces `detectKey()` byte-identically, so adopting it changes nothing until a
caller opts in.

Still open for Compass: contract-test the engine, and the three-publishers-one-bus
coherence question (Auto Pitch refuses to publish a manual key, Compass publishes
it, Granulator publishes it opt-in).

---

## Suite

49 plugins, ~39 experimental, 2 at validation, 0 at RC. The architecture — 28
modules, four rack hosts, the session bus — is the real asset. Catalog is lopsided
(Mix has 14 components, Auto Pitch has 2).

---

## Recommended next actions, in order

1. **Gate 3 listening.** User-only, and the last substantive gate on the flagship.
2. **Settle the latency report + dead parameters** (task #69). RC decision.
3. **Gate 4 manual matrix**, after Step 0 folder cleanup.
4. **Gate 5 RC review.**
5. **Compass held-out corpus across genres** — real work, but should not jump ahead
   of finishing the flagship.
6. GitHub billing — user's account action.

---

## Working lessons that cost time to learn

- **Measure CPU with `CLOCK_THREAD_CPUTIME_ID`, never wall clock.** Wall time
  produced a 4.4x run-to-run spread and two false "Studio mode is too slow"
  warnings that were carried forward as an RC blocker for days. Withdrawn in
  `b4f8eb9`.
- **Check `git diff HEAD`, not a session-start `git status`.** A CI fix that
  existed locally but uncommitted made a local repro succeed and taught nothing.
- **Read the `#define`, not the prose around it.** `SATTARI_KEY_PROFILE 2` falls to
  `#else` = Aarden, not Krumhansl. A whole finding was written against the wrong
  profile.
- **Absence of a word is not absence of a gate.** A Granulator finding came from
  `grep -c confidence` returning 0 and had to be withdrawn; it publishes a
  user-set key, so there is no estimate to gate.
- **Train/test contamination is the default here, not the exception.** Assume any
  corpus in this repo is training data until proven otherwise.
- **A null test measures sample identity, not audibility.** Useful for alignment
  and PDC; it cannot tell you whether something sounds bad.
- **Build dirs**: `build/all` is a dead cache. Use `build/local`, or the
  purpose-built trees like `build/auto-pitch-neural`.
