---
type: release-candidate-hardening-record
product: auto-pitch + song-key
date: 2026-08-16
candidate_revision: uncommitted RC2 working tree based on 767becadb7cdb40dcecf87260de3fd6463861e26
versions: Auto Pitch 0.5.0; Sattari Key 0.2.0
truth_label: machine-qualified internal test build; listening and DAW gates remain open
status: READY FOR USER LISTENING AT 48 KHZ / 512; NOT RELEASE-READY
---

# Auto Pitch 0.5.0 RC2 — engine hardening

## Why this is a new candidate

The 0.4.1 candidate remains preserved at `7fef544a6d49`. No listening scores
existed when the user requested further engine work, so 0.5.0 is a separate RC2
line with new audio, new objective evidence and a new blinded package. The old
sealed listening package must not be overwritten or combined with this one.

## Engine changes

- Rubber Band `OptionEngineFiner` is selected and asserted as R3. The previous
  “quality” configuration silently remained on the default R2 engine.
- Correction Off and genuine zero Amount select an untouched, delay-aligned dry
  reference while keeping R3 warm for click-free re-entry.
- R3 reconstruction group delay is included in both the host latency report and
  the dry reference. At 48 kHz the report is 5,428 samples / 113.08 ms.
- The two legacy live-engine parameter IDs remain readable for session recall,
  but are named inactive, non-automatable and absent from the engine selector.
  The failed moving-period PSOLA path is not presented as a shipping capability.
- YIN/CREPE fusion accepts agreement, folds neural octave estimates near YIN,
  rejects conflicts, and requires confidence plus continuity for neural-only
  rescue. CREPE cannot independently steer AutoKey.

## Auto Pitch + Sattari Key coherence

- Both products call the shared Aarden/chroma `KeyAnalysisEngine` and use the
  shared `0.10` release confidence threshold.
- Low-confidence AutoKey returns **No Result** and retains the visible manual
  key instead of retuning to a guess.
- `keynet` / `keynet_spec` loading is quarantined in both release paths. On the
  only held-out evaluation, `keynet_spec` scored 37.5% exact versus 85.4% for
  Aarden; model validity is no longer converted to false 100% confidence.

## Objective evidence

### Strict dry/null contract

`AutoPitchRender --nulltest`:

```text
correction OFF, steady                  -inf dBFS  PASS  reported 5428; offset +0
correction OFF, vibrato                 -inf dBFS  PASS  reported 5428; offset +0
Amount 0, steady                        -inf dBFS  PASS  reported 5428; offset +0
Amount 0, vibrato                       -inf dBFS  PASS  reported 5428; offset +0
Amount 0, legacy liveEngine locked      -inf dBFS  PASS  reported 5428; offset +0
```

### Reconstructed path and benchmark

`AutoPitchBench all`, 48 kHz / 512 samples:

```text
steady correction error       mean 0.01 cents; max 0.03 cents             PASS
dry/reference latency          5428 reported; 5428 measured                PASS
forced-unity R3 latency        5428 reported; 5402 measured; 0.54 ms       PASS
tuned-onset group delay        5428 reported; 5455 measured; 0.56 ms       PASS
THD / alias proxy              0.01% in-tune; 0.00% corrected / alias      PASS
Studio CPU p95                 3.192 ms vs 10.67 ms budget; 3.4x headroom  PASS
Studio + Neural CPU p95        3.205 ms vs 10.67 ms budget                 PASS
transition timing              monotonic; 8.0 ms block-size spread         PASS
vibrato preservation           89% retained                               PASS
polyphonic stress              96% worst-note movement                     PASS
safety                         100 renders; peak 0.9955; finite; 0 faults  PASS
```

The declared test envelope is intentionally conservative: 44.1/48 kHz at 256
samples or above. 44.1 kHz / 128 passed this machine; 48 kHz / 128 and both
rates at 64 were warnings. 96 kHz was warning/fail across the matrix and is not
approved for this print-quality candidate.

### Suite regression and installed formats

```text
ctest --test-dir build/local --output-on-failure -j4
100% tests passed, 0 tests failed out of 96
```

The new pitch-fusion contract, shared key-engine contract, Auto Pitch core,
Auto Pitch smoke/high-tracking, Sattari Key smoke and all non-StemDeck suite
tests passed. AU, VST3 and Standalone universal binaries were built and copied
locally for both products. Apple `auval` passed Auto Pitch 0.5.0 (`aufx/Ap01`)
and Sattari Key 0.2.0 (`aumf/Sk01`). No distribution signing/notarization or
StemDeck work was performed as part of RC qualification; JUCE's automatic local
ad-hoc bundle seal is only build mechanics.

### Real-vocal package

All six cleared clips and all nine required coverage tags rendered successfully.
The new 18-item blind package is
`/Users/lillypad/Desktop/vocal test/listening-package-rc2`; it is separate from
the preserved 0.4.1 package, level-matched within each set, 24-bit PCM and guarded
against accidental overwrite. The key was not opened and all listening scores
remain `TBD`. See `2026-08-16-rc2-real-vocal-qualification.md`.

## What this evidence does not prove

- It does not prove Antares or BandLab sound-quality parity.
- It does not replace the user's blind listening scores on six real vocal clips.
- It does not validate Logic, Ableton or REAPER state, automation, PDC, bypass,
  multi-instance, ARA or offline-bounce behavior.
- R3/Finer is a high-latency print path, not a live monitoring engine.
- The key confidence threshold still needs a broader held-out contemporary-genre
  calibration set.

## Test order for the user

1. Begin at 48 kHz / 512 samples with Sattari Key on the beat/instrumental and
   **Publish to suite** enabled only after a confident or manual decision.
2. In Auto Pitch, leave **Follow Song** off for manual-key A/B, then enable it and
   confirm the displayed source changes to Sattari Key.
3. Score the new RC2 listening package without opening its key.
4. Only after listening, run the DAW host matrix. Report exact timestamps and the
   preset/settings for distortion, wrong-note pulls, consonant damage or lag.

## Release boundary

RC2 is objectively ready for structured internal listening. It is not approved
for sale, competitive parity language or release until user listening and the
manual DAW matrix pass. The working tree must be committed before it can be
called an immutable candidate revision.
