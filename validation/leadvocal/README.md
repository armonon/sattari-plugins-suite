# Lead-vocal corpus (Auto Pitch / Auto Key benchmarking)

The lead-vocal benchmark corpus for Key Estimator 2.0 and Auto Pitch, following
`validation/key-estimator-2/2026-09-25-corpus-plan.md`. It has a licence on every item and a
sealed holdout split by singer and song.

**No audio is stored in git.** Audio lives under `~/datasets` (and `/Library/Audio/Apple Loops`
for the Apple Loops items). This directory holds the manifest, the loader, the builder, the
coverage report and the holdout access log.

| file | what |
|---|---|
| `manifest.json` | header (split rule, seed, derivation rules, counts) + one record per item |
| `corpus.py` | **the only way to read the corpus**: `items(split, allow_holdout=False, source=None, set=None)` |
| `holdout-access.log` | append-only audit log of every holdout opening (committed; `*.log` is un-ignored for this file) |
| `build_manifest.py` | rebuilds `manifest.json` from the datasets (deterministic; seed 20260927) |
| `coverage.py` | rebuilds `coverage.json` and the coverage block below |

## Sources and licences

| source | licence (`licence_url` per item) | `use` | set | what it is |
|---|---|---|---|---|
| **N20EMv2** (Gu et al., TOMM 2024; [zenodo 10814703](https://zenodo.org/records/10814703)) | CC BY-SA 4.0 (Zenodo licence field, checked via the API 2026-09-27) | internal-eval | primary | 157 whole-song lead vocals (8 h 22 min), 31 singer ids, 20 songs, expert-corrected note annotations. Isolated vocals, plus an aligned accompaniment track per song |
| **vocadito** ([zenodo 5578807](https://zenodo.org/records/5578807)) | CC BY 4.0 | internal-eval | primary | 40 solo excerpts (13.6 min), 29 singers, 7 languages. Two note annotators and an annotated f0 |
| **Apple Loops** lead phrases (`accuracy_20260923/al_manifest.json`) | Logic Pro licence: internal evaluation only, never redistributed | internal-eval-logic-licence | primary (lead / ad-lib); secondary (harmony / background) | 282 phrase groups (69 min). Apple key, genre and descriptor labels. No notes |
| **Choral Singing Dataset** sopranos ([zenodo 2649950](https://zenodo.org/records/2649950)) | CC BY 4.0 | internal-eval | secondary (adversarial) | 12 tracks (4 sopranos x 3 pieces). Annotated f0, section-level notes, bleed from other singers |

N20EMv2 notes:
- **Licence caveat.** The v1 release of the same recordings ([zenodo 7545968](https://zenodo.org/records/7545968))
  is CC BY-NC-SA 4.0 and says "commercial usage is strictly prohibited". v2 is labelled
  CC BY-SA 4.0. Our use is internal R&D evaluation with no redistribution, so both licences
  permit it. Before N20EM results become product evidence, the owner should confirm which
  licence governs.
- **Sample rate.** v2 ships `vocals.wav` at **16 kHz, mono, float32** for all 157 songs (checked
  on every file; the paper downsampled the 44.1 kHz studio takes). Every N20EM item has
  `sr_limited: true`. The items are fine for pitch, note and key metrics. They are **not valid
  for listening or artifact tests**, because nothing above 8 kHz survives.
- **What was downloaded.** `annotations.json` (1.4 MB; md5 verified) and the `vocals.wav` +
  `accomp.wav` members of `data_sub1..10.zip`, fetched with HTTP range reads (zip CRC-32 verified
  on extraction; `~/datasets/n20emv2/fetch_audio_members.py`, `PROVENANCE.txt`). That is 3.6 GB on
  disk. The `video_50fps.npy` lip-video members (about 7.9 GB of the 11.5 GB record) were not
  downloaded because no benchmark uses them.
- **Metadata.** The v2 record ships no singer or song metadata beyond the id `M.D.x-S[f]`:
  - `M.D.x` = singer id (31 ids; the paper reports 30 singers, 17 male and 13 female).
  - `S` = song 1-20 (the NUS-48E songs).
  - `f` = female arrangement.

  Song titles were identified from the first lyric lines in the v1 `metadata_split_by_song.json`
  (only that file was read, via a range request). Genre is the genre of the original recording
  of the identified title. It is not measured from these amateur covers. There is no
  sex, key or genre metadata in the dataset.

## Manifest fields (per item)

The plan's schema, plus derived fields:
- **Plan schema:** `id, source, set, licence, licence_url, use, split, path, singer_id, song_id, sex, register, genre, duration_s, key_truth, key_truth_method, key_ambiguous, notes_available, f0_available, has_bleed`.
- **Derived:** `sex_inferred, sample_rate, sr_limited, note_stats, vibrato, ornaments, pace, key_fit, key_windows, modulation_candidates, modulation_confirmed, arrangement_group, prior_use`.

Rules (also in the manifest header):
- **sex** is set only where the source labels it (Apple Loops `Male`/`Female`). `sex_inferred` is
  pitch-based and used only for balancing and coverage:
  - N20EM and vocadito: the singer-level duration-weighted median annotated MIDI (< 59 M, > 59 F).
    N20EM singers fall into two clean groups (medians 50-58 and 60-65, with nothing in between).
    That gives 17 / 14 ids, against the paper's 17 M / 13 F with 31 ids for 30 people.
  - CSD: the soprano voice part.
- **register** = the item's median MIDI (from annotated notes, or from the reference f0 for Apple
  Loops) against sex:
  - M: low < 50 (D3) <= mid <= 57 (A3) < high.
  - F: low < 60 (C4) <= mid <= 67 (G4) < high.
  - `register_basis` says whether labelled or inferred sex was used.
- **key_truth**
  - N20EM: the singers sang to backing tracks in a fixed key per arrangement. Performances of
    one song are grouped by pitch-class histogram (r >= 0.85) into arrangement groups: 63 groups,
    35 of them with >= 2 singers, covering 129/157 items. key_truth is the duration-weighted
    Temperley fit of the **pooled** notes of the group.
    - `key_fit_item` keeps the single-performance fit. It agrees with the pooled key for
      111/157 items, so per-performance fits alone would be about 30% noisier.
    - margin = r(best) - r(runner-up); `key_ambiguous` when margin < 0.05 (mostly relative
      major/minor).
  - vocadito and CSD: the item's own fit of annotator-1 / section notes.
  - Apple Loops: Apple's label (unverified). The f0 Temperley fit names the same key for only
    64/180 items, which is weak evidence, because 6 s phrases underdetermine a key. Voiced time
    inside the labelled scale has a median of 0.81, above N20EM's 0.76 against derived keys, so
    nothing points at wrong labels.
- **Time-varying key.** `key_windows` holds 30 s windows (10 s hop) with >= 3 consecutive
  confident windows.
  - `modulation_candidates` are same-mode segments >= 50 s whose tonic lies 1-2 st from the
    global key.
  - A candidate is `modulation_confirmed` when >= half of an arrangement group share it.
  - Result: 2 candidates and **0 confirmed** modulations. These 20 songs carry no usable
    within-song key-change truth.
- **Reference f0.**
  - vocadito and CSD: annotated f0.
  - N20EM and Apple Loops: CREPE-tiny with viterbi decoding at 10 ms, computed here. Cached in
    `~/datasets/leadvocal_cache`. N20EM ships no f0.
  - Check: annotated N20EM notes agree with the reference f0 within 50 c (octave-folded) for a
    median of 85% of notes per song (minimum 69%), so annotations and audio are aligned.
- **vibrato**
  - Per sustained segment >= 0.5 s (note cores where notes exist, otherwise voiced runs), the
    pitch is detrended with a 0.4 s running median.
  - A segment has vibrato when the 3.5-9 Hz band holds >= 50% of the residual power above
    2 Hz and the extent is >= 20 c.
  - `extent_cents_pp_median` is the peak-to-peak extent (2*sqrt(2) x rms of the 3-10 Hz band);
    `rate_hz_median` is the rate.
  - Bucket: none (< 15% of sustained time), narrow (< 50 c), medium (50-100 c), wide (>= 100 c),
    or unknown (fewer than 3 segments).
  - Checked on vocadito, CREPE-tiny against the annotated f0:
    - Buckets agree on 30/40 items.
    - Where both measure vibrato, extents differ by a median of 5 c.
    - CREPE-tiny **under-detects narrow vibrato**: 4 annotated-narrow items read as none. N20EM
      and Apple Loops narrow-vibrato counts are therefore lower bounds.
- **note_stats**: note count, median / p10 / p90 note length, share of note time in notes >= 1 s,
  share of notes < 150 ms, and the p5-p95 MIDI range.
- **pace** tags (from note_stats):
  - fast: median note < 0.25 s.
  - slow: median note > 0.5 s.
  - sustained: >= 25% of note time in notes >= 1 s.
- **ornaments** (heuristics, notes + f0):
  - `scoop_frac`: notes whose first 20-60 ms sit >= 80 c below the note core and converge within
    200 ms.
  - `portamento_frac`: legato transitions (gap <= 50 ms, leap >= 2 st) with >= 80 ms spent
    between the two pitches.
  - An item counts as scoop-rich at >= 25% (>= 30 notes tested), and portamento-rich at >= 50%
    (>= 10 transitions).
- **Apple Loops identity**
  - Singer proxy: the persona in the title for Jam Pack Voices / Vox Melodics (`Carla`, `Max`,
    ...); otherwise the pack.
  - Song: Vox Melodics titles carry the song name.
  - Role: harmony / background parts go to the secondary set; lead and ad-lib phrases stay
    primary.
  - `path` is `$AL_WORK/<name>.wav`, the concatenation made by `accuracy_20260923/al_vocals.py prep`
    (the loader resolves `$AL_WORK` from `LEADVOCAL_AL_WORK`, defaulting to
    `validation/accuracy_20260923/al_work`). The source CAFs are listed in `source_paths`.
    Apple Loops audio is AAC-in-CAF (lossy).

## Split (singer x song, no leakage)

Every singer **and** every song gets exactly one label: holdout-sealed, validation or dev. Songs
use a canonical `song_id` that is shared across sources, e.g. `love-me-tender` (N20EM song 9 and
vocadito 12) and `twinkle-twinkle` (N20EM song 10 and vocadito 32, "Baa Baa Black Sheep",
same tune).
- An item is kept only when its singer's label equals its song's label. Otherwise it is
  `excluded-crossover`. **No singer and no song appears in two splits.** The holdout singers'
  songs appear nowhere else.
- Items that earlier benchmarks **tuned on** (`al_vocals.py` dev, `vocadito_bench.py` dev) cannot
  enter the sealed holdout; they become `excluded-prior-tuning`.
- Labels come from simulated annealing (4 restarts, seeds 20260927..+3). It:
  - maximises the kept fraction per source;
  - pulls each source's singer shares and kept-minute shares toward holdout 20% / validation
    15% / dev 65%;
  - balances sex (labelled or inferred) and register between splits.
- The secondary Apple Loops parts follow their singer and song labels.
- The CSD sopranos are **dev-only**. `csd_bench.py` has tuned on and repeatedly scored every CSD
  singer, so no CSD item can be a clean sealed holdout.

The price of zero leakage: 44 N20EM performances (28%) are excluded-crossover, because N20EM's
20 songs are each sung by up to 10 of the 31 singers. Another 40 Apple Loops items are
excluded-prior-tuning. Excluded items are never used for anything.

## Sealing rules

1. Dev tooling reads the corpus **only** through `corpus.items()`. Nobody reads `manifest.json`
   directly to get audio paths.
2. `items('holdout-sealed')` (alias `holdout`) raises `HoldoutSealed` unless `allow_holdout=True`
   **and** the environment variable `LEADVOCAL_MILESTONE` holds a non-empty reason.
3. Every opening appends `UTC timestamp, reason, caller file:line:function + argv, user@host,
   item count` to `holdout-access.log`. Commit that log with the milestone result.
4. Nothing is tuned, thresholded or selected on the holdout. It is opened only for milestone
   validation of a frozen candidate.
5. `corpus.metadata_all()` returns every item **without path fields**, for coverage and licence
   accounting only. It cannot be used to render or score audio, so it is not logged.

```python
import sys; sys.path.insert(0, 'validation/leadvocal')
from corpus import items
dev = items('dev', set='primary')            # 250 items
val = items('validation', source='n20emv2')  # 10 items
# milestone only:  LEADVOCAL_MILESTONE="KE2 M1 frozen candidate abc123" python eval.py
#                  items('holdout-sealed', allow_holdout=True)   -> logged
```

The benchmarks written before this corpus (`vocadito_bench.py`, `al_vocals.py`, `csd_bench.py`)
keep their own legacy splits so that their published numbers stay comparable. New lead-vocal
tooling must use this loader.

Rebuild: `~/.venvs/sattari-audio/bin/python validation/leadvocal/build_manifest.py && python3 validation/leadvocal/coverage.py`
(about 5 min cold with f0 caching, 30 s warm). The split is deterministic given the manifest
inputs, but adding items changes it. Treat the committed manifest as the sealed reference and
do not rebuild the split once the holdout has been opened.

## Coverage

Generated by `coverage.py` (primary set unless noted; `*` = inferred sex).

<!-- coverage:start -->
**Split x source (kept items; all sets)** (items / minutes)

|  | dev | validation | holdout-sealed | total |
|---|---|---|---|---|
| primary:al | 140 / 38.7 | 26 / 12.4 | 14 / 9.6 | 180 / 60.7 |
| primary:n20emv2 | 85 / 252.1 | 10 / 41.7 | 18 / 65.5 | 113 / 359.4 |
| primary:vocadito | 25 / 9.1 | 6 / 2.1 | 9 / 2.4 | 40 / 13.6 |
| secondary:al | 62 / 8.2 | - | - | 62 / 8.2 |
| secondary:csd | 12 / 28.8 | - | - | 12 / 28.8 |

Excluded (never used, so no singer or song crosses splits): al excluded-prior-tuning 40, n20emv2 excluded-crossover 44

Primary singers per split: {'dev': 69, 'validation': 18, 'holdout-sealed': 24}; songs per split: {'dev': 104, 'validation': 20, 'holdout-sealed': 28}

**Primary: sex x register (* = inferred sex, ? = unknown)** (items / minutes)

|  | dev | validation | holdout-sealed | total |
|---|---|---|---|---|
| F high | 16 / 2.6 | 1 / 0.2 | 2 / 0.8 | 19 / 3.6 |
| F low | 7 / 1.7 | 1 / 0.3 | 2 / 2.5 | 10 / 4.5 |
| F mid | 50 / 18.9 | 11 / 7.4 | 2 / 2.7 | 63 / 29.0 |
| F* low | 4 / 1.3 | - | 2 / 4.3 | 6 / 5.6 |
| F* mid | 42 / 110.3 | 6 / 16.5 | 8 / 25.2 | 56 / 152.0 |
| M high | 50 / 10.9 | 10 / 4.2 | 7 / 2.6 | 67 / 17.6 |
| M low | 3 / 0.2 | - | - | 3 / 0.2 |
| M mid | 14 / 4.3 | 3 / 0.4 | 1 / 1.1 | 18 / 5.8 |
| M* high | 17 / 33.5 | 1 / 0.4 | 5 / 13.7 | 23 / 47.6 |
| M* low | 3 / 1.0 | 4 / 12.1 | 4 / 1.0 | 11 / 14.2 |
| M* mid | 44 / 115.1 | 5 / 14.9 | 8 / 23.7 | 57 / 153.7 |

**Primary: genre (by source)** (items / minutes)

|  | dev | validation | holdout-sealed | total |
|---|---|---|---|---|
| al: country-folk | 8 / 1.9 | - | 2 / 1.6 | 10 / 3.5 |
| al: electronic | 2 / 0.2 | 1 / 0.4 | - | 3 / 0.6 |
| al: hip-hop | 10 / 2.2 | - | - | 10 / 2.2 |
| al: jazz | 2 / 1.0 | - | 1 / 1.6 | 3 / 2.7 |
| al: orchestral | 4 / 0.8 | 1 / 0.4 | - | 5 / 1.2 |
| al: r&b | 41 / 13.7 | 4 / 0.7 | - | 45 / 14.4 |
| al: rock | 40 / 9.2 | 2 / 0.3 | 5 / 1.9 | 47 / 11.5 |
| al: unknown | 5 / 2.5 | - | 4 / 3.2 | 9 / 5.6 |
| al: urban (apple label: r&b/hip-hop) | 19 / 5.9 | 16 / 10.3 | 2 / 1.3 | 37 / 17.5 |
| al: world | 9 / 1.2 | 2 / 0.4 | - | 11 / 1.5 |
| n20emv2: children | 7 / 13.9 | - | - | 7 / 13.9 |
| n20emv2: country-folk | 7 / 18.4 | - | - | 7 / 18.4 |
| n20emv2: electronic | - | - | 3 / 12.9 | 3 / 12.9 |
| n20emv2: indie | - | 4 / 20.4 | - | 4 / 20.4 |
| n20emv2: musical-theatre | 14 / 31.3 | - | - | 14 / 31.3 |
| n20emv2: pop | 28 / 99.2 | 6 / 21.3 | 15 / 52.6 | 49 / 173.1 |
| n20emv2: rock | 7 / 24.2 | - | - | 7 / 24.2 |
| n20emv2: standard | 7 / 28.1 | - | - | 7 / 28.1 |
| n20emv2: traditional-holiday | 15 / 37.1 | - | - | 15 / 37.1 |
| vocadito: children | 3 / 0.9 | 1 / 0.2 | - | 4 / 1.2 |
| vocadito: country-folk | 6 / 1.9 | - | - | 6 / 1.9 |
| vocadito: pop | 2 / 0.6 | - | - | 2 / 0.6 |
| vocadito: standard | 2 / 1.0 | - | - | 2 / 1.0 |
| vocadito: traditional-holiday | 1 / 0.5 | - | 1 / 0.1 | 2 / 0.6 |
| vocadito: unknown | 11 / 4.3 | 5 / 1.9 | 8 / 2.2 | 24 / 8.4 |

**Primary: vibrato bucket (extent peak-to-peak: narrow < 50 c <= medium < 100 c <= wide)** (items / minutes)

|  | dev | validation | holdout-sealed | total |
|---|---|---|---|---|
| medium | 56 / 84.4 | 8 / 10.8 | 9 / 19.9 | 73 / 115.0 |
| narrow | 50 / 120.9 | 3 / 4.3 | 6 / 20.2 | 59 / 145.4 |
| none | 41 / 69.8 | 10 / 34.8 | 10 / 30.2 | 61 / 134.9 |
| unknown | 84 / 16.9 | 17 / 3.5 | 12 / 3.5 | 113 / 23.8 |
| wide | 19 / 7.9 | 4 / 2.9 | 4 / 3.8 | 27 / 14.6 |

**Primary: median note length** (items / minutes)

|  | dev | validation | holdout-sealed | total |
|---|---|---|---|---|
| 0.25-0.5 s | 43 / 113.4 | 12 / 37.7 | 16 / 54.6 | 71 / 205.7 |
| <0.25 s | 37 / 66.2 | 4 / 6.1 | 11 / 13.3 | 52 / 85.7 |
| >0.5 s | 30 / 81.6 | - | - | 30 / 81.6 |
| no notes | 140 / 38.7 | 26 / 12.4 | 14 / 9.6 | 180 / 60.7 |

**Primary: pace tags (fast: median note < 0.25 s; slow: > 0.5 s; sustained: >= 25% of note time in notes >= 1 s)** (items / minutes)

|  | dev | validation | holdout-sealed | total |
|---|---|---|---|---|
| fast | 37 / 66.2 | 4 / 6.1 | 11 / 13.3 | 52 / 85.7 |
| moderate | 31 / 73.3 | 12 / 37.7 | 9 / 27.0 | 52 / 138.0 |
| no notes | 140 / 38.7 | 26 / 12.4 | 14 / 9.6 | 180 / 60.7 |
| slow | 30 / 81.6 | - | - | 30 / 81.6 |
| sustained | 34 / 105.1 | - | 7 / 27.5 | 41 / 132.6 |

**Primary: ornaments (scoops: >= 25% of notes start >= 80 c low; portamento: >= 50% of legato leaps >= 2 st glide >= 80 ms)** (items / minutes)

|  | dev | validation | holdout-sealed | total |
|---|---|---|---|---|
| none / not measurable | 206 / 162.5 | 35 / 30.8 | 29 / 35.1 | 270 / 228.4 |
| portamento | 23 / 69.3 | 5 / 17.5 | 6 / 21.1 | 34 / 107.9 |
| scoops | 27 / 84.8 | 4 / 14.4 | 8 / 26.9 | 39 / 126.1 |

**Primary: key truth quality** (items / minutes)

|  | dev | validation | holdout-sealed | total |
|---|---|---|---|---|
| ambiguous (margin < 0.05) | 35 / 77.0 | 6 / 10.7 | 8 / 10.8 | 49 / 98.5 |
| clear | 75 / 184.3 | 10 / 33.2 | 19 / 57.1 | 104 / 274.5 |
| label | 140 / 38.7 | 26 / 12.4 | 14 / 9.6 | 180 / 60.7 |

**Primary: sample rate** (items / minutes)

|  | dev | validation | holdout-sealed | total |
|---|---|---|---|---|
| 16000 Hz | 85 / 252.1 | 10 / 41.7 | 18 / 65.5 | 113 / 359.4 |
| 44100 Hz | 25 / 9.1 | 6 / 2.1 | 9 / 2.4 | 40 / 13.6 |
| 44100 Hz (lossy AAC) | 140 / 38.7 | 26 / 12.4 | 14 / 9.6 | 180 / 60.7 |

Apple Loops key labels (unverified): the f0 Temperley fit names the same key for 64/180 primary items (short phrases underdetermine a key, so this is weak evidence either way); voiced time inside the labelled scale: median 0.81, 87 items below 0.80 (suspect label or strongly chromatic phrase).

N20EM key truth: 63 song+arrangement groups (35 pooled over >= 2 singers, covering 129/157 items); the single-performance fit agrees with the pooled key for 111 items; key_ambiguous 39; modulation confirmed across singers: 0 items.

<details><summary>split x sex x register x genre (full cross-table)</summary>

**Primary: sex x register x genre** (items / minutes)

|  | dev | validation | holdout-sealed | total |
|---|---|---|---|---|
| F high | country-folk | - | - | 1 / 0.5 | 1 / 0.5 |
| F high | electronic | 1 / 0.1 | - | - | 1 / 0.1 |
| F high | hip-hop | 1 / 0.2 | - | - | 1 / 0.2 |
| F high | jazz | 1 / 0.3 | - | - | 1 / 0.3 |
| F high | r&b | 5 / 0.9 | 1 / 0.2 | - | 6 / 1.1 |
| F high | rock | 3 / 0.5 | - | - | 3 / 0.5 |
| F high | unknown | - | - | 1 / 0.3 | 1 / 0.3 |
| F high | urban (apple label: r&b/hip-hop) | 4 / 0.7 | - | - | 4 / 0.7 |
| F high | world | 1 / 0.1 | - | - | 1 / 0.1 |
| F low | jazz | - | - | 1 / 1.6 | 1 / 1.6 |
| F low | r&b | 5 / 1.1 | 1 / 0.3 | - | 6 / 1.4 |
| F low | rock | 2 / 0.7 | - | - | 2 / 0.7 |
| F low | urban (apple label: r&b/hip-hop) | - | - | 1 / 0.8 | 1 / 0.8 |
| F mid | jazz | 1 / 0.8 | - | - | 1 / 0.8 |
| F mid | r&b | 18 / 7.0 | 2 / 0.2 | - | 20 / 7.2 |
| F mid | rock | 19 / 5.8 | - | - | 19 / 5.8 |
| F mid | unknown | 5 / 2.5 | - | 2 / 2.7 | 7 / 5.1 |
| F mid | urban (apple label: r&b/hip-hop) | 5 / 2.5 | 7 / 6.8 | - | 12 / 9.3 |
| F mid | world | 2 / 0.4 | 2 / 0.4 | - | 4 / 0.8 |
| F* low | children | 1 / 0.3 | - | - | 1 / 0.3 |
| F* low | country-folk | 1 / 0.3 | - | - | 1 / 0.3 |
| F* low | pop | 1 / 0.3 | - | 1 / 4.0 | 2 / 4.3 |
| F* low | unknown | 1 / 0.4 | - | 1 / 0.3 | 2 / 0.7 |
| F* mid | children | 3 / 6.0 | 1 / 0.2 | - | 4 / 6.2 |
| F* mid | country-folk | 3 / 7.1 | - | - | 3 / 7.1 |
| F* mid | electronic | - | - | 2 / 8.5 | 2 / 8.5 |
| F* mid | indie | - | 2 / 8.9 | - | 2 / 8.9 |
| F* mid | musical-theatre | 6 / 14.4 | - | - | 6 / 14.4 |
| F* mid | pop | 14 / 46.6 | 2 / 6.9 | 4 / 16.0 | 20 / 69.5 |
| F* mid | rock | 2 / 6.5 | - | - | 2 / 6.5 |
| F* mid | standard | 4 / 11.7 | - | - | 4 / 11.7 |
| F* mid | traditional-holiday | 8 / 16.8 | - | - | 8 / 16.8 |
| F* mid | unknown | 2 / 1.2 | 1 / 0.4 | 2 / 0.7 | 5 / 2.3 |
| M high | country-folk | 4 / 1.3 | - | - | 4 / 1.3 |
| M high | electronic | 1 / 0.1 | 1 / 0.4 | - | 2 / 0.5 |
| M high | hip-hop | 2 / 0.4 | - | - | 2 / 0.4 |
| M high | orchestral | 4 / 0.8 | 1 / 0.4 | - | 5 / 1.2 |
| M high | r&b | 10 / 2.7 | - | - | 10 / 2.7 |
| M high | rock | 13 / 2.0 | 1 / 0.3 | 5 / 1.9 | 19 / 4.2 |
| M high | unknown | - | - | 1 / 0.2 | 1 / 0.2 |
| M high | urban (apple label: r&b/hip-hop) | 10 / 2.8 | 7 / 3.1 | 1 / 0.4 | 18 / 6.3 |
| M high | world | 6 / 0.7 | - | - | 6 / 0.7 |
| M low | hip-hop | 2 / 0.2 | - | - | 2 / 0.2 |
| M low | rock | 1 / 0.0 | - | - | 1 / 0.0 |
| M mid | country-folk | 4 / 0.6 | - | 1 / 1.1 | 5 / 1.7 |
| M mid | hip-hop | 5 / 1.4 | - | - | 5 / 1.4 |
| M mid | r&b | 3 / 2.0 | - | - | 3 / 2.0 |
| M mid | rock | 2 / 0.2 | 1 / 0.0 | - | 3 / 0.3 |
| M mid | urban (apple label: r&b/hip-hop) | - | 2 / 0.3 | - | 2 / 0.3 |
| M* high | children | 2 / 0.6 | - | - | 2 / 0.6 |
| M* high | country-folk | 2 / 0.6 | - | - | 2 / 0.6 |
| M* high | pop | 4 / 15.5 | - | 4 / 13.5 | 8 / 29.0 |
| M* high | rock | 3 / 11.1 | - | - | 3 / 11.1 |
| M* high | traditional-holiday | 3 / 4.9 | - | 1 / 0.1 | 4 / 5.1 |
| M* high | unknown | 3 / 0.7 | 1 / 0.4 | - | 4 / 1.1 |
| M* low | country-folk | 1 / 0.2 | - | - | 1 / 0.2 |
| M* low | indie | - | 2 / 11.5 | - | 2 / 11.5 |
| M* low | unknown | 2 / 0.8 | 2 / 0.6 | 4 / 1.0 | 8 / 2.5 |
| M* mid | children | 4 / 7.9 | - | - | 4 / 7.9 |
| M* mid | country-folk | 6 / 12.0 | - | - | 6 / 12.0 |
| M* mid | electronic | - | - | 1 / 4.4 | 1 / 4.4 |
| M* mid | musical-theatre | 8 / 16.9 | - | - | 8 / 16.9 |
| M* mid | pop | 11 / 37.4 | 4 / 14.4 | 6 / 19.0 | 21 / 70.8 |
| M* mid | rock | 2 / 6.6 | - | - | 2 / 6.6 |
| M* mid | standard | 5 / 17.5 | - | - | 5 / 17.5 |
| M* mid | traditional-holiday | 5 / 15.8 | - | - | 5 / 15.8 |
| M* mid | unknown | 3 / 1.1 | 1 / 0.5 | 1 / 0.2 | 5 / 1.8 |

</details>

**Required coverage vs what the corpus holds** (primary set; counts dev / validation / holdout-sealed; thin = < 5 items, < 10 min, or none sealed)

| requirement | status | dev / val / holdout | minutes | note |
|---|---|---|---|---|
| male low | covered | 6/4/4 | 14.4 |  |
| male mid | covered | 58/8/9 | 159.5 |  |
| male high | covered | 67/11/12 | 65.2 |  |
| female low | covered | 11/1/4 | 10.1 |  |
| female mid | covered | 92/17/10 | 181.0 |  |
| female high | thin | 16/1/2 | 3.6 |  |
| breathy | unknown (not derivable from these sources) | - | - | no source labels breathiness; needs annotation or an HNR measure |
| bright | unknown (not derivable from these sources) | - | - | Apple has no "Bright" descriptor on these items |
| dark | thin | 8/1/0 | 1.6 | Apple descriptor label only |
| powerful | covered | 16/14/2 | 12.8 | proxy: Apple descriptor "Intense" |
| inexperienced | covered | 110/16/27 | 373.0 | dataset-level: N20EM untrained-to-amateur, vocadito mixed |
| sustained | covered | 34/0/7 | 132.6 |  |
| fast | covered | 37/4/11 | 85.7 |  |
| slow | thin | 30/0/0 | 81.6 |  |
| vibrato wide | covered | 19/4/4 | 14.6 |  |
| vibrato narrow | covered | 50/3/6 | 145.4 |  |
| scoops | covered | 27/4/8 | 126.1 | heuristic from notes + f0 |
| slides / portamento | covered | 23/5/6 | 107.9 | heuristic from notes + f0 |
| hard / soft consonants | unknown (not derivable from these sources) | - | - | needs phonetic annotation |
| falsetto / chest / mixed | unknown (not derivable from these sources) | - | - | register mechanism is not labelled anywhere |
| genre pop | covered | 30/6/15 | 173.6 |  |
| genre R&B | covered | 60/20/2 | 31.9 |  |
| genre rock | covered | 47/2/5 | 35.6 |  |
| genre electronic | covered | 2/1/3 | 13.5 |  |
| genre indie | thin | 0/4/0 | 20.4 |  |
| genre rap-sung | thin | 10/0/0 | 2.2 | proxy: melodic Apple Loops labelled Hip Hop; no rap-sung lead |
| genre jazz | thin | 2/0/1 | 2.7 |  |
| genre acoustic | covered | 85/21/12 | 42.9 | proxy: Apple descriptor "Acoustic" (instrumentation tag, not genre) |
| genre musical theatre | thin | 14/0/0 | 31.3 |  |
| has bleed | missing | 0/0/0 | 0.0 |  |
| full-band 44.1/48 kHz whole songs | thin | 0/1/0 | 2.2 |  |
<!-- coverage:end -->

## Gaps and what would fill them

What the corpus does **not** give us today:
- **Studio-quality whole songs at full bandwidth.** N20EM is 16 kHz. Of the full-band sources,
  vocadito is short phone-quality excerpts and Apple Loops is ~6 s AAC phrases. There is
  effectively no 44.1/48 kHz whole-song lead vocal, so listening and artifact tests have no
  cleared material here.
- **Female high** register: thin (about 19 items, 4 min). **Slow** material exists only in dev.
  **Indie**, **musical theatre**, **jazz** and **rap-sung**: thin or single-split, or absent in
  the case of rap-sung, where only melodic Apple Loops labelled Hip Hop exist.
- **Real backing bleed** is absent from the primary set; only the CSD choir bleed exists.
  N20EM's aligned `accomp.wav` can simulate bleed at a controlled SNR, but that is synthetic.
- **Timbre / technique tags** cannot be derived from these sources and are left unknown:
  - breathy and bright;
  - hard / soft consonants;
  - falsetto / chest / mixed.

  Dark and powerful exist only as Apple descriptor labels ("Dark", "Intense").
- **Within-song modulation truth: none.** No confirmed modulation in N20EM, and the plan's
  spliced `key_change_real` clips are song boundaries, not modulations.
- Several vocadito excerpts are not tonal major/minor. Their key truth is flagged ambiguous
  where the margin is small.

What would fill them, in priority order:
1. **Owner's cleared stems** (`validation/corpus/auto-pitch.json`, on the lillypad Mac). These are
   the product target: raw 48 kHz lead takes, rap, belting, soft and noisy, with real bleed. Key
   labels from the owner and notes from our annotation would make them the primary dev and
   holdout source.
2. **ccMixter a cappellas (CC BY)** with our own annotation. Real pop, R&B and hip-hop leads, some
   with bleed and some full-band. They need key, note and technique annotation by us, plus an
   attribution list. This is the cleared route to rap-sung, R&B, female-high, breathy /
   falsetto tags and full-bandwidth listening material.
3. **Licensed non-commercial sets**, only with owner sign-off, internal research only and never
   the sole evidence for a product claim:
   - Children's Song Dataset: two keys per song with MIDI, so key truth.
   - MedleyDB / MUSDB18: bleed, genre breadth.
   - GTSinger: technique labels for falsetto, breathy, vibrato and glissando.
   - Opencpop / M4Singer.

   These would carry `use: internal-research-NC`.
