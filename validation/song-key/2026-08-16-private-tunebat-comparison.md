---
type: private-development-comparison
product: Sattari Key
date: 2026-08-16
status: DEVELOPMENT-SET KEY/BPM AGREEMENT COMPLETE; NOT GROUND TRUTH; NOT A PARITY CLAIM
public_release_allowed: false
---

# Sattari Key vs TuneBat — private 26-file development comparison

## Protocol

- Source: `/Users/lillypad/Downloads/maso/s/`
- Included: 26 WAV/MP3 files, 930 MB, 8,704.569 seconds (2.42 hours).
- Excluded: `.asd`, `.DS_Store` and every non-audio sidecar.
- Portable ordered content-manifest SHA-256:
  `a84903dbe9dbc4fde791915f937a331e17099a20b73709b96ed6b23a9a29ff1c`.
- Sattari baseline: local `SongKeyRender --analyse-file` before the balanced
  full-mix engine. Every file received full-song HPCP/key and onset/tempo analysis.
- Sattari final: the native `SongKeyEstimator` and the plugin's asynchronous
  `--analyse-file` path, both built from the final 2026-08-16 working tree.
- TuneBat: the same 26 local files selected together in TuneBat Analyzer. TuneBat
  states that Analyzer calculations run in the browser and files are not sent to
  another machine. Only the visible Key and BPM fields were recorded.
- No audio was copied into git or into this record.

TuneBat is a comparison system, not truth. These files have no supplied key/BPM
labels, are related works/versions rather than an independent genre-balanced
sample, and were used while tuning the new tempo selection. The results can find
disagreement and regressions; they cannot establish either product's accuracy.

## Result

| Measure | Result | Honest interpretation |
|---|---:|---|
| Baseline primary-key agreement with TuneBat | 7/26 (26.9%) | The previous Aarden/HPCP decision was not competitive on this set |
| Baseline TuneBat key present in Sattari top two | 15/26 (57.7%) | Historical result, retained to show the size of the change |
| Final native-engine key agreement | 26/26 (100%) | Exact pitch class + mode agreement on this development set; enharmonic spelling normalized |
| Final plugin file/drop path agreement | 26/26 (100%) | End-to-end decode, async analysis and result transfer agree with every recorded TuneBat label |
| BPM within 2 BPM of TuneBat | 26/26 (100%) | Strong same-file development agreement; not held out |
| BPM mean absolute difference | 1.10 BPM | TuneBat displays integer BPM; Sattari retains decimals |
| BPM median absolute difference | 1.18 BPM | Consistent small offset on the dominant ~120 BPM family |

The final key result comes from signal analysis, not filenames or a result table:
mono box-resampling to 16 kHz, area-normalized 4096-sample Hann frames, bounded
interpolated spectral peaks, local whitening, harmonic pitch-class accumulation,
whole-song averaging/gating and balanced major/minor correlation. The two final
native mismatches were traced to a lower-band boundary interpolation error; fixing
that signal-path defect moved the C++ engine from 24/26 to 26/26 without any
per-file exception.

The first tempo pass selected slower 2:3 or half-time metrical layers on seven
files: 19/26 were within 2 BPM and the mean difference was 11.78 BPM. The final
estimator reviews x2 and x3/2 fast layers only when the onset signal contains a
substantial corresponding peak. That changed the seven slower selections without
changing the clean 60/90/120/128/174 BPM contracts. Because this folder informed
that change, the final 26/26 figure is a development result and must not be called
held-out performance.

## Per-file baseline results

`P` means Sattari primary matched TuneBat; `A` means the ranked alternate matched.
Enharmonic spellings are normalized for comparison. This table preserves the old
7/26 engine output for traceability; the final engine matches the TuneBat key in
every row and is locked by the native/plugin 26/26 regressions above.

| File | Sattari primary | Sattari alternate | TuneBat key | Match | Sattari BPM | TuneBat BPM |
|---|---|---|---|:---:|---:|---:|
| WE MAKING BEATS NOW .wav | C# minor | B major | C# minor | P | 118.69 | 120 |
| angels party too.mp3 | B major | G# minor | B major | P | 118.22 | 119 |
| angels roll too.mp3 | G# major | F minor | D# major | — | 118.70 | 120 |
| eat it all away .wav | G major | D major | D major | A | 100.82 | 102 |
| kino.wav | G major | A minor | A minor | A | 118.74 | 120 |
| life is real OUT OF RUSSIA.wav | G major | A minor | A minor | A | 118.73 | 120 |
| life is real TELL ME TELL ME.wav | A major | B minor | A major | P | 118.96 | 120 |
| life is real WE WILL ALL GO ONE DAY.wav | G# major | A# minor | A minor | — | 118.76 | 120 |
| life is real controlla.wav | G minor | F major | F major | A | 118.83 | 120 |
| life is real eatawaymy pain.wav | G major | B minor | D major | — | 100.82 | 102 |
| life is real ffor the olmecs.mp3 | G minor | A# major | F major | — | 119.04 | 120 |
| life is real fka fka.mp3 | G# minor | C# minor | A major | — | 119.18 | 120 |
| life is real let me be.wav | G# major | A# minor | A minor | — | 118.77 | 120 |
| life is real olmecseperate.wav | G minor | A# major | F major | — | 118.82 | 120 |
| life is real scream.wav | C# minor | F# minor | C# minor | P | 118.79 | 120 |
| life is real so lets party.wav | G# major | F minor | C# major | — | 118.80 | 120 |
| life is real tell me big controlla.mp3 | G minor | F major | F major | A | 118.87 | 120 |
| life is real tell me the energy field .mp3 | D# minor | F# major | F# major | A | 118.92 | 120 |
| life is real tell me about it bass.mp3 | A minor | G major | A minor | P | 118.77 | 120 |
| life is real tell me anither nightabout it bass.mp3 | G major | D major | A major | — | 118.59 | 119 |
| life is real yerasta ginmati.wav | C# minor | G# minor | E major | — | 119.03 | 120 |
| life is real yerasta.wav | C# minor | E major | C# minor | P | 119.15 | 120 |
| life is real- tell me about it.mp3 | A minor | C major | A minor | P | 118.77 | 120 |
| life is real.mp3 | G major | A minor | A minor | A | 119.41 | 120 |
| s.mp3 | C major | A minor | A minor | A | 118.50 | 120 |
| tell me about the bass.mp3 | F minor | D# major | A# minor | — | 118.82 | 120 |

## Decision

The tempo and key implementations are ready for user testing. The product still
shows the primary, alternate, relative relationship, section disagreement and
No Result, and requires explicit **Lock session** before publishing. Do not turn
26/26 development-set agreement into a universal accuracy or TuneBat-parity
claim. This related-song folder directly informed the implementation. The next
accuracy gate is an
untouched, labeled, contemporary cross-genre corpus scored for exact key, MIREX,
top-two accuracy, abstention coverage and confidence calibration.

## Reproduction

```sh
cmake --build build/local --target SattariSongKeyEstimatorContract SattariKeyDatasetEval SongKeyRender -j 8
build/local/SattariSongKeyEstimatorContract
build/local/core/SattariKeyDatasetEval \
  validation/song-key/tunebat-development-labels \
  /Users/lillypad/Downloads/maso/s
build/local/plugins/song-key/SongKeyRender --analyse-file <local-audio-file>
```

Final focused results: estimator contract PASS; native evaluator 26/26; plugin
file path 26/26; file workflow PASS; live key proof PASS; live tempo PASS. No
audio was uploaded, published or committed.
