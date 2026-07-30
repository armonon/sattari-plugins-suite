# StemDeck real-music corpus

The audio corpus is intentionally not committed. Put licensed or privately owned
full-length files beside a version-1 `corpus.json` manifest and run:

```bash
cmake --build build/stemdeck --target StemDeckCorpusGate
```

```bash
./build/stemdeck/StemDeckCorpusGate /path/to/corpus.json build/stemdeck/corpus-report.json
```

Use `corpus.example.json` as the schema template. Paths may be absolute or
relative to the manifest.

## What the gate grades

**The effective answer — what the app would actually use.** `MainPluginProcessor`
runs the DSP analyser first and then lets the neural beat/key models override it,
but only above their confidence floors. The gate reproduces that selection
exactly and decides PASS/FAIL on the result.

It also reports the DSP and neural answers separately, so the report can settle
the question the confidence floors exist for: **is the model earning its
override?** That is not knowable from synthetic signals, which is the whole
reason this gate exists.

If ONNX is not compiled in, or a model file is missing, the run still grades the
DSP path and prints:

```
NOTE: a neural model was unavailable, so this run did NOT certify the neural path.
```

## What it refuses

| Check | Rule |
|---|---|
| Truth label | `sourceType` must be `real_music` |
| **Generated audio** | measured, not declared — see below |
| Per-track length | `minimumDurationSeconds`, default 120 |
| Corpus size | `minimumTracks` (5) and `minimumMinutes` (20) |
| Shallow grading | at least two of bpm/key/meter graded per track, on average |

That last one matters: a manifest specifying only `expectedBpm` used to print
`REAL_CORPUS_OK`, which reads as "accuracy certified" when only tempo was ever
checked.

### Generated audio is measured, not taken on trust

A `sourceType` string is a declaration, not evidence, and this project's specific
recurring failure is certifying on synthetic signals. So every file is profiled:

- **`hfEnergyRatio`** — energy above 8 kHz over total energy. A real recording
  always has some: hiss, room tone, cymbal bleed. A generated tone has
  mathematically none. Floor: `1e-5`.
- **`spectralChange`** — mean frame-to-frame change in log-magnitude, measured
  only over bins above −60 dB of the frame peak. Catches held tones and looped
  buffers. Floor: `0.02`.

Either one failing rejects the track. Both measured values go into the report so
a false positive is diagnosable rather than mysterious.

Measured on this repo's own fixture shapes:

| Signal | hfEnergyRatio | spectralChange | Verdict |
|---|---|---|---|
| sine + amplitude envelope | 0.0 | 0.10 | rejected |
| broadband noise | 0.64 | 0.30 | accepted |
| bass + harmonics + transients | 3.6e-3 | 1.20 | accepted |

Those numbers come from the detector's own self-test, which runs with the
ordinary contract suite:

```bash
ctest --test-dir build/stemdeck -R StemDeckCorpusSelfTest --output-on-failure
```

It is wired into CI rather than run by hand because it caught a real defect the
first time it ran: the detector was originally an AND of both tells, and an
amplitude-modulated sine cleared the movement test while having zero HF — so
this repo's own fixture would have passed as real music.

**Scope limit, stated plainly:** the self-test validates only the synthetic side
of the thresholds. Whether real music ever trips them cannot be known until a
real corpus exists. If a legitimate track is rejected, read `hfEnergyRatio` and
`spectralChange` from the report before changing a floor.

## Truth annotation

BPM, key and meter expectations must come from an independent trusted
annotation, **not** from StemDeck itself. Grading the app against its own output
measures self-consistency, not accuracy.
