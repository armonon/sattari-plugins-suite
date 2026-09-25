# Auto Pitch + Auto Key: release readiness, 2026-09-25

**Release rule (owner, 2026-09-25):** the exact TAGGED candidate commit must be the one that passes
listening, DAW validation, plugin validation, clean-machine install, signing/notarization and the
final real-singer accuracy run. Evidence from other commits, even adjacent ones, does not count.
Machine evidence for a commit: `scripts/qualify_auto_pitch.sh` ->
`validation/auto-pitch/qualification/<date>-<sha>.md` (latest: `2026-09-25-cbd1ce4.md`, all pass).
The correction engine is frozen unless a release test finds a regression.

Branch `fix/autopitch-accuracy-20260923` (PR #4). Machine gates are run locally, because GitHub
Actions refuses to start the jobs: "recent account payments have failed or your spending limit
needs to be increased". Each result names the commit it was built from.

## Machine gates: all PASS

- Suite checks (45e9635): product catalog, `suite_gate_check.py`, brand registry sync, and all 24
  pure-C++ core contracts.
- `validate_sources.sh`: PASS at a782dc8.
  - Before that it failed on main as well: it required StemDeck `PluginHostManager`, which was
    removed in 3384c63.
- Auto Pitch focused CI job (311c045, universal build with the ONNX runtime):
  - configure, build and embedded-runtime verification of the AU/VST3/Standalone bundles
  - `AutoPitchCoreContract`: 0 FAIL, including the new AutoKey fallback and No Result strength
    regressions
  - editor visual contract (6 renders)
  - `AutoPitchObjectiveBench`: 192 renders, 0 violations
  - `AutoPitchRender --smoke` and `--hitracktest`
- `AutoPitchRender` render modes (311c045):
  - `--nulltest` nulls to -inf dB, and `--livetest` passes.
  - `--gainstage` and `--wetalign` exit 0. `--wetalign` has no threshold; its worst wet shift is
    69 samples (1.44 ms) at Amount 100.
- Strict golden null: PASS at a782dc8 (residual 4.9e-7).
  - It failed from 0cde25d onwards, because target hold is an intended change to the vibrato-glide
    input. Re-blessed with that bisect recorded in the commit.
- Song Key:
  - Focused build and `SongKeyRender --smoke`: PASS.
  - `SattariSongKeyEstimatorContract`: PASS at a782dc8. Before that the focused CI tree did not
    define it ("unknown target", on main too).
- Root tree: cross-plugin session bus contract and `ctest -L ui` (20/20): PASS.
- Plugin validation of the installed build (311c045): auval, pluginval strictness 5, 30 pedalboard
  load/unload cycles, and reported latency 2885 samples (unchanged).
- Not run (disk space): the full 71-target `verify_all.sh` build and run, and the universal root
  build.

## No-harm matrix (`AutoPitchNoHarm`, added 2026-09-25): all 8 checks PASS at cbd1ce4

It found five real defects, all fixed in cbd1ce4 with no change to steady-state output (48/48
real-singer renders bit-identical, AutoKey off and on):
- Humanize was not reproducible between bounces.
- Saved state grew on every save/load, and deleted pitch notes came back.
- Presets carried harmony pan/level over, and three preset names showed mojibake.
- Enable Correction switched with a click.
- Re-engaging host bypass replayed pre-bypass audio and clicked. The engine now keeps running
  while bypassed, which costs CPU during bypass.

Soak: 60 min verified here; the 120-min soak is an operator run (see the qualification report).

## Accuracy on real singers

Choral Singing Dataset, CC BY 4.0, split by singer. Classic style. Figures are notes within 10 c,
dev / holdout (`validation/accuracy_20260923/README.md`).

- Manual Chromatic: 71.2 / 76.7%.
- Manual correct key: 79.6 / 82.0%. The realtime ceiling with the key set is about 82%, so
  production is within about 2–3 points of it.
- AutoKey on, 311c045: **72.5 / 77.6%**. Before the fix it was 59.2 / 64.5%, 12 points below
  Chromatic.
- AutoKey coverage:
  - Confident on about 33% of notes.
  - The wrong key is applied on 7.7 / 6.0% of notes.
  - Soprano melodies: right key 48% of the time; the raw verdict is right 78% of the time.
  - Inner choir parts: 14–18%.
- AutoKey-off output is bit-identical to the pre-PR build on all 48 tracks.
- No numeric accuracy gate exists in the release docs. The standard applied here: no regression
  on real singers, and holdout confirms every dev gain.

## Release-candidate baseline and qualification order (owner, 2026-09-25 15:48)

Baseline: **cbd1ce4**, tag `auto-pitch-rc-baseline-20260925` (not a release tag). The correction DSP
is frozen: no threshold, Humanize, attack/release or strength changes. DSP changes only if blind
listening finds a reproducible defect, DAW testing finds a host problem, the 2-hour soak fails, the
reference A/B shows a regression, or a release test finds a correctness bug.

Order:
1. 2-hour soak on the exact candidate: `scripts/soak_auto_pitch.sh cbd1ce4`. It refuses unless
   the binary sha256 matches the report, and it appends the result to the report.
2. Blind listening package, current candidate: `validation/listening/2026-09-25-listening-package-howto.md`.
3. Auto-Tune Pro A/B (owner renders feed the same package).
4. Logic / Ableton / FL matrix: `validation/daw/2026-09-25-daw-qualification-checklist.md`, after
   `scripts/install_candidate.sh cbd1ce4`.
5. Two non-developer musicians finish real projects.
6. Clean-machine install: `validation/clean-machine/2026-09-25-clean-machine-checklist.md`.
7. Signing / notarization.
8. Rubber Band commercial licensing decision.
9. GitHub Actions billing, then remote CI.
10. Exact release tag and hosted artifact. That tag reruns, or inherits, only evidence proven
    against the identical binary or commit.

Sattari Key: the copy installed on the dev Mac (built 2026-09-24, provenance unknown) is NOT the
candidate. The candidate build is `build/song-key-rc`, built from the cbd1ce4 sources (AU
`f67c04d5`, VST3 `fa448044`). It passes its estimator contract, render smoke and pluginval
strictness 5. `install_candidate.sh` installs it; quit all DAWs first.

Permanent regressions added after cbd1ce4 (tests only; plugin sources unchanged). Both fail if
their bug is reinserted:
- `AutoPitchCoreContract` statePersistence: 5 save/load cycles stay stable; deleted pitch notes
  stay deleted; key map and MIDI Learn survive; a legacy session with duplicate sections restores
  the newest copy and saves back in canonical form.
- Humanize: bouncing twice gives the same output; save/reopen gives the same output; separate
  instances get distinct seeds, so a doubled vocal is not identical; a legacy project migrates
  and is reproducible after its next save.

## Needs the owner (blocking)

1. Fix GitHub Actions billing so remote CI can run.
2. Blind listening on current audio. The RC3 package (08-16) predates every change since. The
   cleared clips in `validation/corpus/auto-pitch.json` live only on the lillypad machine
   (`/Users/lillypad/Desktop/vocal test/`). Either run `scripts/qualify_auto_pitch_corpus.py`
   there, or supply the clips.
3. A/B against Auto-Tune Pro on the same material (reference renders).
4. DAW host matrix in Logic, Ableton and FL for Auto Pitch and Sattari Key. Only an
   insert/bypass/remove smoke test was done, before the AutoKey commits.
5. Developer ID Application and Installer certificates, a notary profile, and
   sign/notarize/staple.
6. Licensing: a Rubber Band commercial licence (or GPL compliance), JUCE, and notices.
7. A tagged release commit, update-manifest SHA-256 and size, and hosting.
8. Two non-developer musicians finishing a project; a clean-machine
   install/update/rollback/uninstall.

## Known characteristics (intentional)

- **Host bypass keeps the engine running (soft bypass).**
  - While a host bypasses Auto Pitch, the full pitch engine keeps processing a copy of the input
    and its output is discarded. So a bypassed instance costs about the same CPU as an active one.
  - The bypass output is exactly the input delayed by the reported latency.
  - Bypass is entered and left with a 10 ms crossfade, and re-engaging never replays pre-bypass
    audio.
  - Chosen over a re-prime gap or stale-audio risk (owner, 2026-09-25). Not to be optimized before
    release unless the DAW matrix shows a significant problem.
- Enable Correction / Amount 0 and back crossfade over 10 ms.
- Humanize is deterministic per session: the seed is saved with the project, and each instance
  has its own seed.

## Known limitations (documented, not blocking by any stated gate)

- AutoKey coverage is limited by the raw estimator.
  - Longer memory, an out-of-key veto and persistence gating were tested and not shipped
    (Round 11).
  - Solo-vocal AutoKey stays a convenience. A chosen key or Sattari Key is the precise path.
- CPU: 96 kHz at 128 samples or less fails the budget. Extreme Live is outside the budget at
  96 kHz / 64.
- The Classic lead offset (-20 ms) was calibrated at 512 samples only.
- The synthetic key corpus favours Krumhansl by construction. It is secondary evidence only.

## Stale evidence superseded here

These predate the 09-23 to 09-25 changes and should not be cited for the current build:
`vocalbench-results/FINAL_RELEASE_VERDICT.md` (08-21), the RC3 real-vocal qualification (08-16),
`validation/PRODUCT_RC_REPORT.md` (08-25), and the Song Key 08-18 and 09-02 evidence.
