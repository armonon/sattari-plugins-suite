# Auto Pitch harmony candidate — 21 September 2026

Base: `32b7c09155f8a2bd949c60a9c68ce55448ad0309` plus the existing dirty
working tree and the changes below. This is not a committed shipping artifact.
Pre-existing Claude/user/release-audit changes were preserved. No plug-ins were
installed, signed, notarized, published or pushed. StemDeck was not changed.

## Implemented

- Live lead bypass now keeps the selected bank's tracker/readout active. The
  previous Correction Off/Amount-zero branch warmed the wrong bank in Extreme
  mode and did not publish pitch frames, making Live harmony silent. The exact
  delayed dry reference remains the audible lead bypass.
- Humanization's 180 ms refresh and pitch/timing smoothing now advance by samples,
  not callbacks. Vibrato/control intervals remain continuous across host blocks.
  Reset restores the deterministic seed. Timing humanization actually varies
  over time instead of applying only a fixed per-voice offset.
- MIDI and graph targets outside the octave are octave-folded, preserving note
  class. A final source-relative fold handles large lead-note edits: harmony
  shifts untouched input, not the already corrected lead.
- H1–H4 pitch/mute overrides reach the renderer independently of the lead and
  support undo/redo, splits and project recall. Drag changes pitch, Shift-click
  mutes/restores, right-click restores automatic generation, Alt permits selecting
  a harmony that overlaps the lead. Expired regions no longer retain overrides.
- An audio-owned lock-free snapshot replaces the UI's separate harmony planner.
- Four appended formant parameters drive independent Studio envelope shifters.
  The 1024-sample stage uses existing lead-alignment headroom; Live disables it
  without changing reported latency. The UI updates availability immediately on
  workspace entry and continues to track mode changes.
- Older projects restore absent formant parameters to neutral. Repeated saves
  replace old PitchEdits/KeyMap extension trees instead of appending duplicates
  that could restore stale edits. The latter was found by the new recall test.
- Added renderer, processor, history, UI and local cleared-corpus regression tools.

Usage and deliberate limits: [harmony guide](../../docs/auto-pitch-harmony.md).

## Verification

**Final automated gate: PASS; public release/listening approval: still open.**

- Fresh build-first native suite: **115/115 PASS**, including Auto Pitch,
  polyphonic, key handoff, arrangement/ARA fixture and editor contracts.
  Evidence: `full-contracts-bypass-fixed.log`. Universal Release targets were
  rebuilt with StemDeck disabled; only test/shared-code targets were built,
  not installed AU/VST3 wrappers. This is not a clean-checkout package test.
- Focused native processor run: PASS, including six new bypass-harmony cases
  (Correction Off and Amount zero in Extreme, Ultra and Range), project/history
  recall, graph gestures, source-range folding, and minimum-size UI state.
  Evidence: `bypass-integration.log`.
- Renderer and complete processor integration also PASS on the x86_64 slice
  under Rosetta. This is not physical Intel hardware qualification; translated
  CPU timings are not used. Evidence: `renderer-rosetta.log` and
  `integration-rosetta.log`.
- Correction Off/Amount-zero dry-reference null: **8/8 PASS**, sample-exact at
  the reported delay, including Live Mode. This tests bypass, not corrected
  vocal naturalness or the separate wet-onset alignment question.
  Evidence: `bypass-null.log`.
- Cleared real-vocal matrix: **24/24 PASS** (six complete clips × Live
  Extreme/Studio × one/four voices, 44.1/48 kHz, 256-sample blocks). Zero
  non-finite or clipped samples; every case is non-silent; maximum peak 0.232736.
  These are raw safety results at explicit −12 dB input / −9 dB per-voice gains,
  not maximum-level stress or musical-accuracy/naturalness scores. Lead is muted,
  lead correction is off, chromatic intervals are +4/−3/+7/−7, and modulation is
  off. Evidence: `vocal-matrix-bypass-fixed.log` and
  [metric/source-hash summary](2026-09-21-harmony-evidence/vocal-matrix-summary.json).
- Minimum-size Harmony and Edit layouts were rendered and visually inspected;
  the new formant sliders fit. Processor tests additionally verify their actual
  availability after preparation in Live/Studio.

Private audio: `/Users/lillypad/Desktop/vocal test/harmony-candidate-20260921-6egbW4/verified/`
(235 MB). Its manifest identifies the exact executable and six input hashes.
Audio was never copied into Git or uploaded; the old sealed listening package
is unchanged. The accompanying README distinguishes the failed diagnostic run.

The corpus and integration executable SHA-256 is
`91de565516a39a446c38f0370baea134b1ae680e3183ef47c00a1111691f43e0`.
All recorded final source and executable hashes were checked after testing:
[source fingerprints](2026-09-21-harmony-evidence/source-qualified-sha256.txt),
[executable fingerprints](2026-09-21-harmony-evidence/binary-qualified-sha256.txt).
These identify the local test candidate, not a signed shipping bundle.

Completed renderer checks in `2026-09-21-harmony-evidence/renderer-final.log`:

- Four simultaneous voices with pitch/timing humanization, vibrato and ensemble:
  zero maximum sample difference at 17/64/256/1024 samples, in both Range and
  Extreme banks; exact reset repeatability; finite, unclipped synthetic output.
- A synthetic 220 Hz vowel retains a measured 220.18 Hz fundamental with −7/+7
  formant shifts, while the brightness proxy changes in the expected directions.
- Four-voice renderer-only thread CPU, Apple M3 / macOS 26.5.1, 48 kHz/64 frames:
  Range p95 0.0460–0.0472 ms, Extreme p95 0.0763 ms, versus a 1.3333 ms callback.
  Maximum observed was 0.1805 ms. Measured with CLOCK_THREAD_CPUTIME_ID under the
  current machine load; no wall-time substitution, whole-plug-in CPU, Studio
  formant CPU, interface round trip or long-session DAW guarantee is implied.

An intermediate full build passed 114/115 registered tests and exposed stale
graph recall plus initial Live formant-control state. Those failures are retained
in `full-contracts.log`. An earlier mixed-object development build crashed and
was superseded by the consistent rebuild; its failures remain in
`focused-tests.log`. Interrupted development builds are not passes.

The subsequent `full-contracts-final.log` passed 115/115, but the additional real
vocal matrix found a missing contract: all 12 Live cases were silent with lead
correction off, while all 12 Studio cases produced finite, unclipped audio. This
was a production bypass-path defect, not a reason to turn correction back on in
the test. Six new cases were added for Correction Off and Amount zero in all
three Live profiles, and the corpus matrix was repeated. The
first matrix remains in `vocal-matrix.log` and its private output manifest.
The corrected candidate passes both the expanded contracts and all 24 rerenders
above; no threshold was weakened and correction was left off in the rerun.

## Remaining release gates

- The user's real-vocal listening approval, against this exact candidate. No
  scores or commercial-equivalence claims are inferred from machine checks.
- Target-DAW scan, automation, state, transport, mode-switch and bounce testing
  of actual shipping bundles; none were installed by this pass.
- The renderer remains a four-voice monophonic-vocal harmonizer, approximately
  one octave around the source. Independent timing clips, separate stem export,
  arbitrary multi-octave shifts and low-latency formants are not implemented.
- Auto Key supplies tonal context, not an infallible chord progression. Global
  key accuracy and harmony naturalness are separate qualification questions.
