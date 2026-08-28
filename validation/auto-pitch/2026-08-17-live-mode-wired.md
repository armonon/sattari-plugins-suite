---
type: feature-validation
product: auto-pitch
date: 2026-08-17
lifecycle: alpha (Live Mode only; the print path is untouched)
truth_label: internal validation only; nothing here is a shipping claim
status: WIRED AND MEASURED — no listening evidence yet
---

# Auto Pitch — Live Mode connected to the plugin

`SattariLivePitchEngine` existed and passed its isolated contract, but was not
reachable from the plugin. It is now selectable, and it takes its key from the
same place the print path does — which means from Sattari Key over the session
bus when Follow Song Key is on.

Run it with `AutoPitchRender --livetest`.

## Results

```
  1. print path (Live Mode OFF)
       reported latency : 2885 samples
       corrected to     : +0.0 cents from A3

  2. Live Mode latency  : 1037 samples (21.60 ms)
  3. Live Mode corrects : input +45 cents -> output +0.1 cents

  4. Follow Song Key: same audio, two keys on the session bus
       bus key C major   -> output   +0.1 cents from A3
       bus key Db major  -> output +100.0 cents from A3
       difference: 99.9 cents
       PASS: the bus key reaches the live planner, and it overrode the
             manual key (G) that was also set.
```

**21.60 ms against the print path's 60.10 ms.** The engine's own contract
measures 15.17 ms at its default 100 Hz floor; Auto Pitch prepares it at 70 Hz
for wide vocal coverage, and the extra 6.4 ms is the cost of tracking down to a
low male fundamental. That is a deliberate trade and it is adjustable.

## How the key arrives, and why there is no second code path

`processWithLiveEngine()` builds the planner's 12-note mask from
`currentCorrectionRootIndex()` and `effectiveScaleIndex()` — the same two
accessors `buildCorrectionFrame()` uses for the print engine. Those already carry
the whole precedence chain:

    Follow Song Key (session bus / Sattari Key) > Key Map > AutoKey > manual

so Live Mode inherits the shared key rather than re-deriving it. The Edit Notes
mask is intersected on top, with the same "all notes disabled means no
correction, never silence-on-pitch" fallback.

Test 4 is the part that cannot be established by reading the code: the same audio
is rendered twice with Follow Song Key on, against two keys published to the real
bus under the `song-key` publisher id. The keys are chosen to **disagree** about
the input note — A3+45 cents is diatonic in C major (snaps to A) and not in Db
major (snaps to Bb, a semitone away). A broken link would produce two identical
renders; a working one produces a semitone of difference, which is what it does.

An earlier version of this test used C major and D major. A is diatonic in both,
so it could not have failed. Recorded because a test that cannot fail is worse
than no test.

## The print path did not move

This is the constraint that mattered, because the RC3 listening package is built
against the print path and would be void if it changed.

| check | result |
|---|---|
| `--smoke` render | pass, peak 0.2576, 0 gross clicks |
| `--nulltest`, five print rows | `-inf` dBFS at offset +0, reported 2885 |
| `--wetalign` | -93 samples, unchanged |
| `AutoPitchCoreContract` | pass, `engine=rubberband` |
| CPU p95 @ 48k/512 | 2.28 / 2.21 / 2.29 ms vs 10.67 ms budget |

Live Mode is a **new `liveMode` parameter, default OFF**. It is not a revival of
the legacy `liveEngine` id: that one meant the PSOLA path locked off after it
failed real-vocal validation, and reusing the id would have handed a different
engine to any old session that saved it on. Legacy stays inert.

## Alignment in Live Mode

The delayed dry reference now follows whichever engine is active. Pinned to the
Rubber Band figure it would have been ~1,850 samples late in Live Mode — the same
class of latency-report mismatch that failed Gate 4 test A at 0.4.1. Three new
`--nulltest` rows cover it:

```
  correction OFF, LIVE MODE, vibrato    -inf  PASS  (reported 1037; +0)
  Amount 0, LIVE MODE, steady           -inf  PASS  (reported 1037; +0)
  Amount 0, LIVE MODE, vibrato 50ct     -inf  PASS  (reported 1037; +0)
```

The ring is sized in `prepareToPlay` for the larger of the two engine delays, so
toggling Live Mode while the transport rolls moves a read offset and never
allocates on the audio thread. Entering or leaving Live Mode flushes both
pipelines for the same reason the legacy swap does.

## A bug worth recording

The first wiring reduced every correction to 1% of what was asked for, and read
exactly like "the engine does not work". Cause: `amount` and `speed` are **0..1**
parameters, and were mapped as if they were 0..100. The isolated contract passing
while the plugin did nothing is what located it — the engine was never the
suspect after that.

## What this is NOT

- **No listening evidence.** Every number here is machine-measured. Live Mode has
  not been heard by anyone on a real voice.
- **No host testing.** Not in Logic, Ableton, or REAPER; the round-trip latency a
  singer actually experiences includes the interface, which this does not measure.
- **Not a low-latency monitoring claim.** 21.60 ms is the plugin's reported
  delay, not a product claim, and it is above the ~15 ms the engine can reach at a
  higher pitch floor.
- **Stereo is coherent but untested on real material.** Channel 0 plans and the
  others follow its scale, so the channels cannot drift apart. Verified by
  construction, not by measurement on a stereo source.

Live Mode stays `alpha`. The print path remains the qualification candidate.
