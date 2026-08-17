# Sattari — session handoff

Last synced baseline: 2026-08-16 at `767beca`, branch
`harmony-engine-and-portal-compass`, PR #1. Auto Pitch 0.5.0 RC3, Sattari Key
0.3.0, shared-key adoption, the new contracts and qualification records below
are local working-tree changes until they are committed and pushed.

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

## Auto Pitch — 0.5.0 RC3, new line based on `767beca`

The earlier 0.4.1 candidate is preserved at `7fef544a6d49`; its sealed listening
package remains untouched. Before any 0.4.1 scores existed, the user explicitly
requested further hardening, so 0.5.0 RC3 is a new working line and must receive
new scores. It is not yet committed; do not change DSP again once listening begins.

```
GATE 0 lifecycle     PASS       remains validation
GATE 1 performance   PASS       R3 CPU p95 5.97 ms vs 10.67 ms at 48k/512 under load
GATE 2 corpus        PASS       6 clips, all 9 required tags
GATE 3 listening     READY      new 18-item RC3 package; USER'S TASK
GATE 4 offline       PASS       dry null + reconstructed group delay + auval
GATE 4 hosts         OPEN       Logic/Ableton/REAPER manual matrix
GATE 5 RC review     BLOCKED    on user listening + manual hosts
```

### What RC3 changed

- Explicit Rubber Band R3/Finer Short vocal engine; old code accidentally stayed on R2.
- Correction Off and zero Amount select untouched delay-aligned dry audio.
- Five strict null cases are `-inf` dBFS at zero offset.
- 2,885 samples reported at 48 kHz; forced-unity R3 and tuned-onset group delay
  both land within 0.77 ms.
- Controlled Hard-mode vibrato falls from 19.7 to 4.0 cents after measured
  -30 ms moving-control compensation; a sustained belt reaches 2.3 cents median.
- Conservative YIN/CREPE fusion; CREPE validates or rescues YIN without smearing
  an agreeing YIN contour, and cannot independently steer AutoKey.
- Auto Pitch's frozen internal fallback remains Aarden/chroma. Sattari Key now
  uses a separate balanced 16 kHz full-mix engine, preserves the 0.10 margin and
  No Result policy, and hands an explicitly locked result to Auto Pitch over the
  existing session bus. The held-out-losing neural path remains absent.
- Legacy live controls remain recall-compatible but are visibly inactive and
  non-automatable. **Live Mode is now connected** (2026-08-17): a NEW `liveMode`
  parameter, default OFF, selecting `SattariLivePitchEngine` at 1,037 samples /
  21.60 ms against the print path's 60.10 ms. It takes its key from
  `currentCorrectionRootIndex()` / `effectiveScaleIndex()`, so Follow Song Key
  reaches it from Sattari Key over the session bus with no second code path —
  verified against two disagreeing keys on the real bus. The print path is
  bit-identical (`--nulltest` still `-inf` at 2885, smoke and contract pass).
  `AutoPitchRender --livetest`; record in
  `validation/auto-pitch/2026-08-17-live-mode-wired.md`. Still alpha: no
  listening evidence, no host testing, and no low-latency monitoring claim.
- Auto Pitch 0.5.0 and the previous Sattari Key 0.2.0 universal AU/VST3/Standalone
  builds are installed locally. Both AUs passed `auval`; Sattari Key 0.3.0 still
  needs the full packaging/host pass after its focused validation closes.

Primary records:
`validation/auto-pitch/2026-08-16-rc2-engine-hardening.md` and
`validation/auto-pitch/2026-08-16-rc3-fast-correction-hardening.md`, with the
final corpus inventory in
`validation/auto-pitch/2026-08-16-rc3-real-vocal-qualification.md`. The isolated
next-generation live work is recorded in
`validation/auto-pitch/2026-08-16-live-engine-alpha-01.md`.

### Gate 3 — the critical path

Use only `~/Desktop/vocal test/listening-package-0.5.0-rc3/`: 18 level-matched
24-bit WAVs, six sets A-F. The old `listening-package/` is the 0.4.1 candidate.
The generator refuses to overwrite a non-empty package, and the RC3 key has not been
opened. Only the user can score it; never fabricate or infer scores.

### Gate 4 — remaining manual work

Offline PDC/pass-through and Apple component validation are closed. State,
automation, transport, bypass, editor, multi-instance, offline bounce and ARA
still need real DAWs via
`validation/daw/2026-08-10-auto-pitch-host-matrix.md`. Start at 48 kHz / 512;
R3/Finer Short is a 60.10 ms print path. Begin at 48 kHz / 512; 64/128-sample
and 96 kHz sessions are not approved for RC3.

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

The shared engine implementation contract is now **PASS**:
`validation/compass/2026-08-16-key-analysis-engine-contract.md`. It locks default
byte-equivalence, all six runtime profiles, validity/source semantics, model-free
fallback, and deterministic whole-buffer analysis. This is not an accuracy or
lifecycle promotion claim.

Sattari Key 0.3.0 now adds asynchronous full-song selection, drag/drop, explicit
live listening, 50%-overlap live coverage, per-section agreement/modulation
warning, Camelot notation, ranked alternate/relative key, file/live tempo and a
frozen **Lock session** publication step. The balanced, model-free full-mix key
engine now matches all 26 recorded TuneBat key/mode results through both the
native evaluator and plugin file path; BPM remains 26/26 within 2 BPM. The old
engine's baseline was 7/26 primary and 15/26 top-two. This is development-set
agreement, not ground truth or a universal parity claim. The focused record is
`validation/song-key/2026-08-16-full-song-and-live-analysis.md`. Workflow is
implemented; a clean labeled contemporary cross-genre comparison remains the
release accuracy gate.

Still open for Compass: the three-publishers-one-bus coherence question (Auto
Pitch refuses to publish a manual key, Compass publishes it, Granulator publishes
it opt-in), plus the held-out cross-genre accuracy corpus.

---

## Suite

49 plugins, ~39 experimental, 2 at validation, 0 at RC. The architecture — 28
modules, four rack hosts, the session bus — is the real asset. Catalog is lopsided
(Mix has 14 components, Auto Pitch has 2).

---

## Recommended next actions, in order

1. **Freeze/commit the RC3 worktree before any more DSP change.** The current
   listening package is labeled to the working tree, not an immutable commit.
2. **Gate 3 RC3 listening.** User-only; use `listening-package-0.5.0-rc3` and do not open
   its key early.
3. **Gate 4 manual matrix**, after Step 0 folder cleanup, at 48 kHz / 512 first.
4. **Gate 5 RC review** using the actual listening and DAW evidence.
5. **Build a real low-latency epoch/phase engine** only after RC3 print quality is
   scored; do not unlock the failed PSOLA path.
6. **Compass held-out corpus across genres** — real work, but should not jump ahead
   of finishing the flagship.
7. GitHub billing — user's account action.

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
