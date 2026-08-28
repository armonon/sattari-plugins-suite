---
type: implementation-and-validation-record
product: Sattari Key
version: 0.3.0
date: 2026-08-16
status: FILE/LIVE/LOCK/TEMPO WORKFLOW COMPLETE; PRIVATE 26/26 DEVELOPMENT AGREEMENT; HELD-OUT GATE OPEN
---

# Sattari Key 0.3.0 — full-song and live analysis

## Outcome

Sattari Key now has the three intended entry paths:

- **Choose song** opens a native file picker.
- **Drop song here** accepts one supported audio file anywhere on the source card.
- **Listen live** analyses the plugin's main input without changing the audio.

Whole-song work is asynchronous, cancellable and independent of the audio
callback. A new file replaces an in-flight scan safely. The UI reports progress,
the source filename, the global key, mode, Camelot code, tuning offset, decision
margin and agreement across 15-second sections. Material whose section verdicts
meaningfully disagree is marked as a possible key change. Re-scan applies to the
current source; Listen live returns immediately to the host/standalone input.

The review surface now also shows the ranked second key, identifies a relative
major/minor relationship, and estimates tempo for selected files and live input.
Live mode uses exact host BPM when the DAW provides it. **Lock session** is a
deliberate confirmation: it freezes key, mode, reference tuning and tempo into a
snapshot, then publishes that snapshot to the suite. Continued detection cannot
move the session bus until the user unlocks and locks again. Manual override is
still available and also requires the lock action.

## Accuracy design

The file path decodes to mono in bounded chunks and feeds a deterministic full-mix
engine: box-resampling to 16 kHz, area-normalized 4096-sample Hann frames, the 60
strongest interpolated peaks from 25 Hz to 3.5 kHz, local whitening, four-harmonic
pitch-class accumulation, whole-song averaging, 0.2 peak-relative gating and a
balanced major/minor correlation decision. A lower-band peak is held at the
25 Hz boundary instead of being interpolated out of band; this was the final
native-vs-reference discrepancy. Fifteen-second decisions use the same engine.
The older full-rate HPCP accumulator remains only for tuning-reference and tempo
support, not the global key verdict.

The live worker advances when another half-window of audio has arrived and feeds
its snapshots to the same 16 kHz full-mix estimator. A high-margin direct-chroma
resolver handles an exposed sustained chord, where a full-song whitening profile
has too little harmonic context. Both paths stay off the audio callback.

Tempo uses a complete spectral-flux onset envelope for files and a bounded
120-second envelope for live analysis. Local-floor removal, normalized
autocorrelation, harmonic reinforcement and sub-hop interpolation produce the
base estimate. A conservative metrical review can select x2 or x3/2 only when a
substantial corresponding onset peak exists; the UI warns on close half/double
evidence. The estimator does not run on the audio callback.

The suite publish rule remains conservative: a result below the shared 0.10
winner-versus-runner-up margin is visible as **No Result** and cannot overwrite
Auto Pitch or another follower. Manual override remains explicit.

## Evidence and claim boundary

- The production Aarden/HPCP path previously measured **85.4% exact / 88.12
  MIREXx100** on the genuinely held-out 48-track Schubert Winterreise set.
- That set contains one composer and piano/voice classical material. It cannot
  establish contemporary pop, rock, hip-hop or electronic accuracy.
- GiantSteps and GTZAN are useful regression corpora but are training-contaminated
  in this repository. Their scores are not release claims.
- The current 500-track GiantSteps regression measured Aarden/HPCP at **39.8%
  exact / 47.34 MIREXx100**. This is a reproducibility baseline, not held-out
  product evidence.
- The private 26-file same-data development comparison now finds **26/26 exact
  key/mode agreements** in both the native evaluator and the plugin file path,
  plus **26/26 BPM results within 2 BPM of TuneBat** (1.10 BPM mean absolute
  difference). The old key engine measured 7/26 primary and 15/26 top-two.
- TuneBat is not ground truth, the files are related versions, and this folder
  directly informed the implementation. Therefore 26/26 is a regression target,
  not held-out accuracy or a universal parity claim. See
  `validation/song-key/2026-08-16-private-tunebat-comparison.md`.

This milestone establishes a professional analysis workflow and exact agreement
on the declared private development set. A public accuracy/parity claim still
requires a licensed, untouched, cross-genre corpus scored against both systems
under the same labels.

## Verification

```sh
cmake --build build/local --target SongKeyRender -j 4
cmake --build build/local --target SattariSongKeyEstimatorContract SattariKeyDatasetEval -j 4
build/local/SattariSongKeyEstimatorContract
build/local/core/SattariKeyDatasetEval validation/song-key/tunebat-development-labels /Users/lillypad/Downloads/maso/s
build/local/plugins/song-key/SongKeyRender --smoke
build/local/plugins/song-key/SongKeyRender --file-smoke
build/local/plugins/song-key/SongKeyRender --live-tempo-smoke
build/local/plugins/song-key/SongKeyRender --analyse-file <audio-file>
ctest --test-dir build/local -R 'Sattari(AudioCoreTempo|KeyAnalysis)|SongKey(Render|FileAnalysis|LiveTempo)' --output-on-failure
```

Focused results on 2026-08-16:

- Legacy live detection smoke: PASS, C major, margin 0.279.
- Async full-song file smoke: PASS, including ranked alternate, 120 BPM,
  explicit lock/publication and File -> Live unlock/return.
- Live tempo smoke: PASS at 120 BPM.
- Tempo contract: PASS at 60, 90, 120, 128 and 174 BPM plus syncopated 126 BPM;
  silence and short evidence correctly return no result.
- Shared key contract: PASS, including a distinct ranked alternate and no
  alternate for invalid input.
- Balanced song-key contract: PASS across all 24 keys at 44.1/48 kHz, decoder
  chunk-boundary invariance, 25 Hz sub-bass boundary regression and silence
  abstention.
- Native private-set evaluation: PASS, 26/26 exact key/mode agreements.
- End-to-end plugin file analysis: PASS, 26/26 exact key/mode agreements.
- Real GiantSteps WAV decode: PASS; the first labeled sample returned exact C minor.
- Real MP3 decode: PASS.
- Normal and minimum editor render: PASS at 840x650 and 720x600.

## Remaining accuracy/product gates

1. Acquire or construct an untouched, commercially usable labeled corpus spanning
   contemporary genres, production density, tuning drift and modulating songs.
2. Score exact key, MIREX, abstention coverage and confidence calibration; report
   all four, not accuracy alone.
3. Repeat tempo comparison on a genuinely held-out, labeled and independent set;
   this private development folder cannot be reused as release evidence.
4. Add an audition keyboard and, later, an explicit key-change timeline editor.
5. Extend the session-bus schema with decision kind, confidence, scope and
   optional key-map timeline; the current frozen snapshot uses the compatible
   base key/tempo/reference message.

No Auto Pitch DSP, Compass decision path, StemDeck code, signing or notarization
was changed by this work. Auto Pitch can consume the explicitly locked Sattari
Key result through the existing session bus while its frozen internal fallback
remains unchanged.
