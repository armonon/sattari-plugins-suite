# Auto Pitch / Auto Key accuracy benchmark (2026-09-23)

Audio is not committed; every set regenerates from these scripts.

## Pitch: real-sounding voices (ElevenLabs)
- `gen_voices.py`: 24 dry a cappella singers, bass to soprano (API key read from macOS Keychain
  `com.sattari.elevenlabs.api-key`, never written anywhere).
- `pitchref.py`: CREPE-full reference pitch on the Apple GPU (torchcrepe, MPS).
- `accuracy_pitch.py`: every held note detuned by its own known offset (±20–45 c, rubberband
  `--pitchmap`), run through `AutoPitchBench` (512-sample blocks). A note passes within 10 c.
  `VOICES=tune|holdout` splits odd and even voices.
- `fail_cats.py` sorts failures into categories. `oracle.py` scores a perfect per-note correction:
  it only reaches ~90% on this metric, because natural singing moves within a note.

| Build | Tuning voices | Held-out voices |
|---|---|---|
| origin/main (f27e4fc) | 72.4% | 80.6% |
| + target hysteresis (0cde25d) | 81.1% | 85.7% |

## Pitch: synthetic singers (exact ground truth)
- `synth_singer.py`: formant-weighted additive voice with vibrato, scoops, portamento, jitter and breath;
  the pitch contour is known exactly.
- `synth_pitch.py make|input|oracle|score <tag> [bench args]`: 18 singers, 288 notes.

| | within 10 c |
|---|---|
| input (validates metric) | 1.4% (median 29.5 c off) |
| oracle perfect correction | 98.3% |
| Auto Pitch, Classic | ~81% |
| Auto Pitch, Classic Mode | 84.0% |

Remaining gap: note starts (legato/scoop target flicker; the request settles in ~180 ms at p90),
very low bass notes below the voicing threshold, and the intent gate in Classic style.

## Key: synthetic singers in all 24 keys
- `synth_key.py`: 96 clips (4 voices × 24 keys; a cappella and voice + chords). Each melody is
  regenerated until Krumhansl-Schmuckler on its own notes picks the label with a margin, so every
  label is musically unambiguous. `ks_check.py` audits this.
- `score_key.py`: Sattari Key file analysis. `SongKeyRender --corpus synth_key` gives the live path.
- `score_autokey_p.py <profile|default> tune|holdout|all`, `score_autokey_rt.py`: Auto Pitch AutoKey.

| Detector | Exact | a cappella | voice + chords |
|---|---|---|---|
| Sattari Key file analysis | 89/96 | 41/48 | 48/48 |
| Sattari Key live (`--corpus`) | 78/96 | | |
| Auto Pitch AutoKey, origin/main | 25/96 | 19/48 | 6/48 |
| Auto Pitch AutoKey, 9f6d57b | 85/96 | 48/48 | 37/48 |

## Round 3 (evening): bigger synthetic set and rejected experiments
The synthetic set is now 36 singers / 576 notes (tune = seeds 0–2, holdout = seeds 3–5); 288 notes
per half is needed to see gains above noise. Baseline (9f6d57b): tune 81.3%, holdout 77.8%.

- Almost the whole gap to the oracle is the first 100–200 ms of each note: scoring the core from
  200 ms instead of 100 ms gives 95.5% vs the oracle's 97.9%.
- Onset-aware hold (follow the nearest note during note starts): tune +0, holdout +1. Rejected.
- Retune speed (Classic Mode, Transition = 1): no change. Rejected.
- Studio look-ahead (delay audio into Rubber Band so analysis runs ahead): synthetic holdout
  77.8% → 90.6%, BUT held-out real voices 86.2% → 71.0%. Synthetic notes are steady with step
  changes, so early correction catches the steps. Real voices move continuously, and applying a
  correction ~60 ms before the audio it was measured on adds error. Rejected; code kept in
  `experiments_onset_lookahead.patch`.
- Frame-level metrics on real voices are not usable: a perfect per-frame hard-tune scores 39.8%
  (Auto Pitch 54%) because CREPE on shifted audio is noisy (`frame_metric.py`, `flat_oracle.py`).

Takeaway: synthetic singers find real bugs but can over-reward timing changes. Every pitch change
must also pass the real-voice holdout before it ships.

## Round 4 (2026-09-24): real-singer benchmark and onset diagnosis

### Data (Phase 1)
| Dataset | Source | License | Singers | Material | Note labels | Used |
|---|---|---|---|---|---|---|
| **Choral Singing Dataset** (Cuesta et al. 2018) | zenodo.org/records/2649950 (md5 7a9643609a1c3902b5255d78dbba3303) | **CC BY 4.0** | 16 (4 S/A/T/B), professional | 3 SATB pieces, 48 individual-mic tracks, 44.1 kHz | per-section note onsets/pitch (Tony, manually corrected) + per-track f0 (manually corrected) + MIDI score | **yes** |
| Dagstuhl ChoirSet | zenodo.org/records/4618287 | CC BY 4.0 | amateur choir | 5.1 GB | f0 + score sync | downloading; planned stress-test singers |
| MedleyDB, MIR-1K, GTSinger, M4Singer, Opencpop | various | NC / research-only | — | — | — | rejected: licence doesn't clearly permit company development use |
| `validation/live_pitch_p2/vocal_*.wav` (repo) | agent-generated Aug 2026 | — | — | test signals | none | rejected: not real singing, no labels |
| `~/Sattari/VocalStems/` | drop folder for Armon's stems (+ optional MIDI/key) | — | — | — | — | empty so far |

Ground truth: intended note = the rounded section note pitch, kept only when that note exists in the
MIDI score (1496 of 1560 notes, 96%). The rest sit on a boundary and are excluded.
Reference pitch: CREPE-tiny (median 2.5 c from the manual annotations; CREPE-full 2.2 c; 50x faster).
My fast spectral note-centre shortcut (`notecentre.py`) was 8 c off, so it was rejected.

### Split (Phase 2): by SINGER, `csd_split.json`
dev = singers 1–3 of each section (12 singers, 36 tracks, 3919 notes);
holdout = singer 4 of each section (4 singers, 12 tracks, 1283 notes), evaluated once at the end.

### Baseline R0 = production (bit-identical to the verified build, latency 2885)
| | dev | holdout |
|---|---|---|
| notes within 10 c (core mean) | 71.2% | 76.7% |
| within 5 c | 43.1% | 40.0% |
| perfect per-note correction (ceiling) | 95.5% | 95.4% |
| singers uncorrected | 31.7% | 37.4% |
| frames within 10 c: 0–100 ms / 100–200 / 200+ | 34.0 / 46.9 / 54.8% | 35.6 / 48.6 / 58.7% |
| wrong-target frames 0–25 / 75–100 / 150–200 / 300+ ms | 50 / 28 / 14 / 4.6% | 48 / 27 / 16.5 / 3.9% |
| octave-target frames | 0.1% | ~0% |

### Diagnosis (Phases 3, 4, 6); tools `csd_onset.py`, `csd_onset2.py`, `csd_onset3.py`; traces in `csd_onset/examples`
- **Note entry is a target-selection problem.** Wrong-target frames are 50% at 0–25 ms and 28% at
  75–100 ms. When the target IS right, the correction is accurate (requested − needed ≈ −2 c; 24% of
  the error remains at 0–25 ms, 6% by 150 ms).
- **Much of it is inherent to causal correction.** The singer is not yet nearest the intended note on
  40% of frames at 0–25 ms, 22% at 75–100 ms and 13% at 150–200 ms. R0's wrong-target rate is only ~10
  points above that at 0–25 ms and ~1 point above it by 150 ms. On 38% of failed notes the singer is
  >50 c away for the whole first 200 ms (long scoop, late arrival, or the section-level annotation
  being early for that singer).
- Wrong-target frames: ~44% are the singer's own nearest note (a scoop being followed), ~5–13% hold
  the previous note, and ~48% are "other" (almost always ±1 semitone). Of the "other" frames, 55% are
  the hysteresis hold keeping a neighbour after a scoop, and 45% are detector vs reference
  disagreement near transitions (median 21 c).
- Not the cause: confidence (0.98 on wrong frames), the intent gate (Classic Mode made no change),
  correction magnitude (see above), voicing (plugin unvoiced ≤4% at 0–25 ms, ~0% after 100 ms),
  octave errors (0.1% of frames).
- Failed-note categories (dev / holdout): transition ambiguity 20/27%, late with correct target
  21/25%, vibrato-related wrong target 17/16%, under-corrected 19/9%, scorer ceiling 8/10%, detector
  7/4%, wrong target 4/5%, octave <0.5%. The classifier labels some long scoops as "vibrato"; see the
  example plots.

### Experiments (Phase 7), dev singers only unless stated; code in `experiments_real_vocal_round4.patch`
| | Change | dev within 10 c | onset wrong-target 0–100 ms | Verdict |
|---|---|---|---|---|
| R0 | production | 71.2% | 37.7% | baseline |
| E1 | target from 100 ms moving-average pitch | 74.3% | 41.1% (worse) | gain is steady-state only; onset worse. Rejected |
| E2 | E1 with a fast (20 ms → 100 ms over 150 ms) start | 75.9% (CI +2.6..+7.1, 28/36 tracks up) | 40.6% (worse) | **holdout 76.7 → 77.2% (+0.5, noise), p95 39.7 → 47.8 c.** Did not generalize. Rejected |
| E3 | existing Intent v2 slide prediction | 58.6% | 42.8% | much worse. Rejected |
| E4 | 30 ms target confirmation in the first 200 ms (vs 120 ms) | 70.8% | 37.3% | no change. Rejected |
Earlier rejected (Round 3): Studio look-ahead (real voices 86 → 71%), onset-aware hold, retune speed.

### Conclusion
Keep production. Onset error on real singers is mostly the singer not yet being on the note plus
annotation onset uncertainty. Once the target is right, the correction is fast and accurate. The
steady-state vibrato gain from E1/E2 did not transfer to held-out singers.
Known issue: the CSD "detuned" variant barely detuned (bug in prep); don't use it.

## Round 5 (2026-09-24): where the gap actually is
`causal_ceiling.py`, `key_ceiling.py`, `key_ema_ceiling.py` score target choices analytically (the
output is assumed to land exactly on the chosen target), which separates target choice from shifting.

- **Correction amount/timing costs ~0.** Scoring R0's own chosen targets as if the shift were perfect
  gives 70.9%, vs 71.2% measured. The whole chromatic-mode gap is target choice.
- **Realtime (causal) ceiling, chromatic, perfect detector:** nearest note with no hold 61.3%, with the
  production hold 73.6%, with a 100 ms average 76.0%. The 95.5% oracle knows the intended note; no
  realtime nearest-note tuner can get there on these singers.
- **Key/scale mode (the piece's key from its score, `csd_keys.json`: ER F major, LI C major,
  ND G minor), production code unchanged:**
  | | dev | holdout |
  |---|---|---|
  | chromatic R0 | 71.2% | 76.7% |
  | key mode R0 | **79.6%** | **82.0%** |
  | key mode, perfect-detector ceiling | 82.4% | 84.2% |
  In-key intended notes (91.5%): 86.4% (dev). Out-of-key notes (accidentals, 8.5%): 6%, because key
  mode forces them to the neighbouring scale note. p95 rises to ~63 c from those notes.
- Accidental-aware key mode (accept an out-of-key note when the singer is within 15–30 c of it):
  the ceiling drops (82.4 → 73.5–76.9%). Rejected on paper, not built.
- Averaging inside key mode: ceiling 80.9–81.7% vs 82.4%. Rejected on paper.
- Neural detector in key mode: 79.2% vs 79.6%. No gain.

Conclusion: with the key set, production is within ~2–3 points of the realtime ceiling. The lever
that matters most is getting the KEY right (Auto Key, fixed in 9f6d57b) and using key mode. After
that, the remaining engineering lever is detector accuracy (~3 points).

## Round 6 (2026-09-24): review fixes (branch fix/autopitch-review-20260924)
Scorer: `pitchref.f0` now seeds torchcrepe's per-frame +-20 c dither; unseeded, the same wav
scored twice moved CSD results by up to +-0.5 points (R0 read 71.2/76.7 before, 71.6/75.8 on a
rescore of bit-identical audio). All numbers below use the seeded scorer (`*.s0.npy` caches).

CSD within 10 c, dev / holdout (`csd_bench.py`, Classic, 512 blocks):
| | production (469106f) | branch |
|---|---|---|
| YIN chromatic | 71.1 / 76.8 | bit-identical output (all 48 renders) |
| YIN key mode | 79.2 / 81.6 | bit-identical output |
| Neural chromatic (`--param neuralPitch=1 --nonrealtime`) | 70.3 / 76.0 | 71.2 / 76.1 |
| Neural key mode | 79.1 / 80.8 | 79.2 / 81.5 |

AutoKey, `score_autokey_rt.py` (raw verdict vs the gated key correction USES; fallback A minor):
- synth_key: raw 85/96 (a cappella 48/48); used at the end 65/96 (the 0.10 gate withholds 6 a
  cappella and 15 voice+chords correct verdicts). Unchanged by the branch.
- CSD real singers (48 tracks, sung-note path): raw final 22/48; time used right 19.0%, wrong
  27.1%, fallback 53.8%. Right and wrong verdicts have the same margins (median 0.085 vs 0.083),
  so a lower gate adds wrong-key time (0.06: right 26.0 / wrong 36.9; 0: 40.1 / 58.3). Gate kept.
  For reference, Aarden on this path: raw 24/48, used wrong only 9.5% (right 20.1%).
- Poly chroma path on CSD (`--param polyphonic=1`): used wrong 58.2% -> 37.5%, right 19.0 -> 15.3%
  with the chroma histogram back on Aarden (fed3ca3).
- Histogram memory (`--autokey-memory`, `key_change.py`): 6-10 s follows a modulation sooner
  (half step, key B right 10-20 s after the change: 50% at 15 s, 81% at 10 s, 89% at 6 s) but real
  singers lose: CSD raw right time 40.3% (15 s) -> 35.1% (10 s) -> 26.8% (6 s). Kept 15 s.

## Round 7 (2026-09-24): detector work, measured before changing anything
`det_diag.py`, `det_proto.py`, `det_manual.py`. Key mode, CSD real singers, analytic target-choice score.
- Production detector vs CREPE reference, per frame: bias +0.5 c, median |err| 9.1 c, p90 24 c,
  8.7% of frames > 25 c (13.4% in the first 150 ms of notes, bass section worst). Timing is already
  aligned (best shift 10 ms).
- Causal smoothing of the production readings (median 3/5 blocks, EMA 0.5/0.3, outlier rejection):
  80.0–80.7% vs 80.5%. No effect.
- **Circularity check:** with the dataset's MANUALLY CORRECTED f0 as a "perfect detector", target
  choice scores 78.6% dev / 79.9% holdout. That is LOWER than the production detector (80.5 / 81.7%).
  The CREPE "perfect detector" (82.3 / 83.4%) is inflated because the scorer is also CREPE.
- Conclusion: the detector is not a bottleneck for target choice. The "~3 points from the detector"
  in Round 5 was an artifact of the reference. No detector change is justified by this evidence.
  Remaining key-mode loss to shifting is ~1.3 points (analytic 80.5 vs measured 79.2, seeded scorer).

## Round 8 (2026-09-24): Auto Key for real singers
Scorer: `score_autokey_rt.py --corpus csd` (realtime pace; "used" = the key correction actually
uses after the 0.10 confidence gate; fallback = manual key). Split by singer as in Round 4.

- **The synthetic key corpus is biased toward Krumhansl.** `synth_key.py` keeps only melodies whose
  notes Krumhansl-Schmuckler agrees with, so it rewards Krumhansl by construction. On REAL music
  (each CSD part's exact notes from the MIDI score): Krumhansl 8/12, Aarden 10/12, Temperley 10/12.
- Plugin, sung-note path, real singers:
  | profile | dev final | dev used right / WRONG / fallback | holdout final | holdout used right / WRONG |
  |---|---|---|---|---|
  | Krumhansl (9f6d57b) | 16/36 | 18.1 / 28.1 / 53.9% | 6/12 | 21.9 / 24.4% |
  | Aarden | 18/36 | 19.9 / 9.6 / 70.4% | — | — |
  | **Temperley (shipped)** | **20/36** | **23.1 / 7.9 / 69.1%** | **7/12** | **24.8 / 5.4%** |
- Gate sweep (Temperley, dev): lowering the gate adds wrong-key time about as fast as right (0.06:
  33.1 / 17.9%; 0.02: 43.9 / 33.6%). Kept 0.10.
- Synthetic regression: Temperley 78/96 (a cappella 46/48) vs Krumhansl 85/96. Accepted: real
  singers take priority, and that corpus favours Krumhansl by construction.
- No-harm: contract (incl. AutoKey fixtures), auval, pluginval 5, 20 load/unload cycles, latency 2885.
- Open: fallback is ~70% of the time and falls back to the MANUAL key (plugin default A minor), which
  is itself a wrong key for most songs. Consider falling back to chromatic instead (product decision).

## Round 9 (2026-09-24): Auto Key low-confidence fallback
Decision (Armon): keep Temperley and the 0.10 gate. Below the gate, a manual key the user actually
chose is kept; an untouched default falls back to **Chromatic** instead of A minor.
- Intent is stored explicitly, not inferred from "value == A minor": `manualKeyExplicit` is set by
  any host/UI change to keyRoot / scale / scaleMode. Our own Scale→scaleMode mirror writes and
  `setStateInformation` do not set it. It is saved in state. Sessions saved before this change have
  no flag and load as explicit, so their behaviour does not change.
- Regression (`AutoPitchCoreContract`, autoKeyGate): tonal → detected C major; noise + untouched →
  Chromatic; noise + explicit A minor → A minor; noise + explicit D major → D major; the flag survives
  a state round trip (untouched stays untouched, chosen stays chosen, legacy → explicit).
- Bench: `AutoPitchBench --manual-key-untouched` leaves the key parameters at their defaults.
- Real singers, pitch (Classic, AutoKey on, `csd_bench.py score … --auto-key`), notes within 10 c:
  | fallback | dev 10 c / 5 c / p95 | holdout 10 c / 5 c / p95 |
  |---|---|---|
  | A minor (old: untouched default) | 58.6 / 33.3% / 48.2 c | 63.0 / 34.9% / 50.5 c |
  | **Chromatic (new)** | **59.2 / 33.7% / 40.5 c** | **64.5 / 35.2% / 40.4 c** |
  Traces confirmed `autoKey: true` for both (an earlier run was invalid: zsh passed the flags as one
  argument, AutoKey stayed off and both outputs were identical. Discarded.)
- No-harm: all 48 AutoKey-off renders are bit-identical to R0. Contract, auval, pluginval 5, 30 + 20
  (after librosa) load/unload cycles and latency 2885 all pass.
- Note for context: with AutoKey on, real singers score below plain Chromatic (R0 71.2 / 76.7%) and
  below the correct manual key (Round 5). AutoKey is confident only ~30% of the time and is wrong for
  part of that time (Round 8). The chromatic fallback limits the damage but does not close that gap.
- The synthetic key corpus stays a secondary regression check only (biased toward Krumhansl, Round 8).

## Round 10 (2026-09-25): AutoKey No Result was under-correcting (fixed)
Found by splitting Round 9's AutoKey score per note by the key actually in use (`ak_decomp.py`):
notes in the chromatic fallback scored 49.4% vs 69.7% for the SAME notes with manual Chromatic,
although the trace showed an identical target and correction. Tenors and basses lost up to 40 points.
- Repro: steady C5 +30 c, Classic, Chromatic. Manual: output -0.2 c. AutoKey on (untouched OR an
  explicit key): output +19.5 c, i.e. only ~35% of the correction applied.
- Cause: `estimateIntentConfidence` multiplied strength by a "key trust" factor
  `clamp(conf / 0.10, 0.35, 1)` whenever the key origin was AutoKey. Since the 0.10 gate was
  introduced, a below-gate AutoKey never supplies the key (the manual key or Chromatic does), so the
  factor only ever weakened the fallback, at every No Result moment. Present since 2026-08-19 (ee4d4cc).
- Fix: factor removed (it is identically 1 whenever the detected key is actually used). The status
  line now says "Chromatic until a key is detected" when that is the fallback.
- Regression (`AutoPitchCoreContract`): B3 +45 c abs error manual 0.011 c; AutoKey No Result
  untouched / explicit C major was 29.25 c before, now 0.013 c (must stay within 3 c of manual).
- Real singers, Classic, AutoKey on, chromatic fallback (notes within 10 c):
  | | dev 10 c / 5 c / p95 | holdout 10 c / 5 c / p95 |
  |---|---|---|
  | R0 manual Chromatic, AutoKey off | 71.2 / 43.1% / — | 76.7 / 40.0% / — |
  | AutoKey on, before fix (Round 9) | 59.2 / 33.7% / 40.5 c | 64.5 / 35.2% / 40.4 c |
  | **AutoKey on, fixed** | **72.5 / 44.6% / 41.8 c** | **77.6 / 42.5% / 42.4 c** |
  Per key state (dev): fallback notes 69.5% (manual Chromatic on the same notes 69.7%); right-key
  notes 81.6% (75.1%); wrong-key notes 66.6% (69.2%). Holdout: 73.4 / 86.3 / 83.1%.
- AutoKey on is now better than Chromatic on both splits, instead of 12 points worse. The ceiling
  with the correct key everywhere is 79.6 / 82.0% (Round 5): the remaining gap is coverage (AutoKey is
  confident on ~33% of notes), not the correction.
- No-harm: AutoKey-off renders 48/48 bit-identical to R0; contract, auval, pluginval 5, 30 load/unload
  cycles pass; latency 2885.
- Process note: `cmake --build build/auto-pitch-neural` (default target) does NOT rebuild
  AutoPitchBench / AutoPitchCoreContract / AutoPitchRender / AutoPitchObjectiveBench. Build them
  with `--target` explicitly or they run stale code.

## Round 11 (2026-09-25): AutoKey coverage experiments (none shipped)
Goal: more right-key time without more wrong-key time, gate kept at 0.10. Scored on the note set
(C major == A minor for correction). Tools: `veto_proto.py` (replays bench traces offline),
`ak_conf_features.py`, `key_change.py` (synthetic modulations), `ak_decomp.py`.
- Relative-key ambiguity is NOT the problem: only 7% of raw verdict frames are the relative key;
  41% are a genuinely wrong note set.
- By section (all 48 tracks, used key after the fix): soprano (the melody, closest to a lead vocal)
  right 48.2% / wrong 3.6% / chromatic 48.2%, raw verdict right 77.6%. Alto, tenor, bass: used right
  14–18%, wrong 7–9%, raw right 48–58%. Inner choir parts are the hard case.
- Histogram memory 30 s / 60 s (dev): right-key notes 26.7 -> 33.0 / 37.5%, wrong 7.7 -> 3.8 / 2.5%,
  pitch 72.5 -> 72.8 / 72.8%. Holdout 30 s: right 27.4 -> 34.8%, wrong 6.0 -> 2.2%, pitch 77.6 -> 77.5%.
  But modulations are followed later (used key, median: half step 13.6 -> 20.7 s, up a fourth
  14.5 -> 20.2 s) and the old key is applied for 23.6% of the 20 s after a change (15 s: 16.5%).
  Flat on pitch, worse on modulations: kept 15 s.
- Out-of-key veto (Chromatic while > TAU of the last W s of singing is outside the key), W 4/8 s,
  TAU 0.1/0.2, memory 15/30 s: cuts wrong-key time (best: 30 s, W 8, TAU 0.2 -> dev wrong 1.8%,
  after-modulation wrong 15.2%) but cuts right-key time about as much (dev 32.4 -> 21.8%), because real
  intonation puts plenty of frames outside the right key too. Rejected on paper, not built.
- Verdict persistence as confidence (same verdict for >= P s): at equal wrong-key time it adds only
  ~2–3 points of right-key time over the margin (dev: margin 0.12 R18.8/W4.1 vs persist 10 s + margin
  0.03 R21.7/W4.4). Not worth a behaviour change.
- Conclusion: after the Round 10 fix the correction side is sound; AutoKey on beats Chromatic. More
  coverage needs a better raw estimator (right ~60% of the time on these singers), not gating tricks.

## Round 12 (2026-09-25): real LEAD vocals for AutoKey (Apple Loops)
Source found on this machine: Logic Pro's Apple Loops carry Apple's own labels (category Vocals,
Male/Female, key signature, key type, descriptors) in a CAF metadata chunk. `al_vocals.py` selects
Male/Female + Single + Melodic + major/minor: 970 phrases, grouped by song into 282 clips (93 min;
146 male, 136 female), split BY SONG (dev 212 / holdout 70). Audio stays in /Library (Logic licence:
internal evaluation only, never redistributed); converted copies in `al_work/` (ignored).
Caveats: most clips are a single ~6 s phrase (median 1 phrase/song); loops may be pre-tuned; the key
is Apple's label, not independently verified.
- Production (7a8dd57 behaviour), dev, note-set scoring: used key right 13.6% / wrong 7.4% /
  chromatic 79.0% of voiced time; raw verdict right 43.9%; final verdict right 110/212.
  By length: < 8 s raw 33.6%, final 35/81; 16–40 s raw 63.3%, final 31/42; > 40 s raw 56.4%, 18/30.
- Takeaway: on short lead-vocal phrases AutoKey mostly (correctly) abstains; when it commits it is
  wrong about a third of the time (7.4 vs 13.6). This is the benchmark for any key-detector work.

## Round 13 (2026-09-25): vocadito, a second real-singer pitch benchmark
`vocadito_bench.py`. vocadito (CC BY 4.0, zenodo 5578807, in ~/datasets/vocadito): 40 solo excerpts,
7 languages, singers of mixed training on various devices. Only notes both annotators agree on;
split by singer (dev 26 tracks / 716 notes, holdout 14 / 308). Classic, production at 7a8dd57+.
Singers uncorrected: 25.6 / 29.9% within 10 c (CSD choir: 31.7 / 37.4%).
| mode | dev 10 c / 5 c / p95 | holdout 10 c / 5 c / p95 |
|---|---|---|
| Chromatic | 62.0 / 33.5% / 54.9 c | 64.6 / 32.5% / 57.6 c |
| Key from the notes (Temperley fit, oracle) | 60.4 / 32.1% / 85.3 c | 67.4 / 33.6% / 89.2 c |
| AutoKey on, untouched fallback | 60.1 / 32.3% / 63.7 c | 65.3 / 34.4% / 61.7 c |
- Harder than CSD (untrained singers, phone-quality recordings, wider pitch errors).
- A single major/minor key does not fit several excerpts (p95 85–89 c in keyed mode: notes snapped to
  a neighbouring scale tone), so key mode gives no clear gain here, unlike the choir. AutoKey on is
  within noise of Chromatic (-1.9 dev / +0.7 holdout) with a longer tail than Chromatic.
- Small set (1,024 notes): read differences under ~2 points as noise.

## Round 14 (2026-09-25): real-vocal key changes
`key_change_real.py` (make | run <tag>), replacing reliance on the synthetic `key_change.py`. Each clip
splices REAL lead vocals from `al_manifest.json` (Round 12), one singer sex: key A section (>= 25 s,
songs labelled key A, same pack preferred) then key B section (>= 35 s, other songs), cut at the
quietest point below 40 / 55 s, 0.3 s gaps, levelled; no pitch shifting. Types: up half / whole /
fourth (same mode), parallel, relative, and a no-change control (key A -> other songs in key A).
Dev 60 clips (5 per type x sex), holdout 20 (only dev or only holdout songs per clip; holdout male
has no material for half / whole / fourth). Plus 6 CSD clips (choir piece -> another singer's
piece in another key; 4 dev, 2 holdout). 86 WAVs, 561 MB in `key_change_real/` (ignored).
Traces only (AutoKey on, manual key untouched, block 512, Classic); scored on the NOTE SET, so
relative and none are 'no set change'. Delay = until the used key is B's set to the end (strict)
or B's set with no wrong set afterwards, chromatic allowed (lenient); wrong = a set that is neither
the current section's nor chromatic.
Production (7a8dd57+), AL clips (set change = half/whole/fourth/parallel; no change = relative/none):
| | dev change (40) | dev no change (20) | holdout change (11) | holdout no change (9) |
|---|---|---|---|---|
| followed strict / lenient | 17 / 27 | - | 1 / 5 | - |
| delay median strict / lenient | 25.1 / 18.2 s | - | 34.9 / 20.2 s | - |
| fail-safe (old set gone) median / p90 | 1.4 / 16.4 s | - | 11.7 / 21.6 s | - |
| wrong % pre / 0-10 / 10-30 / 30+ s | 10.0 / 34.1 / 10.8 / 5.8 | 10.8 / 7.8 / 0.1 / 0.0 | 13.6 / 44.1 / 26.8 / 14.4 | 24.6 / 29.7 / 9.1 / 11.7 |
| false transitions / clip, pre / post | 2.3 / 1.9 | 2.4 / 0.3 | 2.1 / 3.6 | 5.1 / 3.0 |
| chatter (set changes / min) | 15.0 | 16.9 | 16.7 | 15.9 |
| coverage (right set) pre / post | 19.9 / 22.7% | 25.2 / 54.5% | 24.0 / 6.9% | 9.8 / 24.8% |
- By type (dev, strict followed, lenient median): fourth 8/10, 20.6 s; whole 4/10, 17.8 s;
  parallel 4/10, 7.8 s; half 1/10, 24.0 s (wrong 63.9% of the first 10 s, mostly the old key held,
  which shares only 2 notes with the new one). CSD dev 2/4 followed, lenient 25.4 s.
- No-change controls: 17/20 dev clips have at least one switch to a wrong set, nearly all before the
  splice (2.4 vs 0.3 per clip after it); dev wrong time is ~0 from 10 s after the splice on.
- Memory 30 s (reference only): dev set change lenient followed 23/40 (27), median 20.3 s (18.2),
  wrong 0-10 / 10-30 s 39.0 / 18.5% (34.1 / 10.8), coverage post 16.9% (22.7); no-change coverage
  post 60.7% (54.5), chatter 14.2 / min (16.9). Slower on modulations again, as in Round 11.
- Takeaways: on real vocals AutoKey mostly abstains (~20% right-set coverage before the splice); a
  modulation leaves the old set applied for several seconds (mean 2.1 s per clip dev, 4.3 s holdout)
  and a half-step change is rarely followed within 35-55 s. Chatter ~15 used-set changes / min
  (chromatic counted as a state), vs ~2-5 switches into a wrong set per clip. Holdout is worse on
  every metric (raw verdict right before the splice 50 / 22% vs 57 / 54% dev); it is small (20
  clips, several share songs) and male-poor.
- Caveats: Apple key labels are unverified and the loops may be pre-tuned. A check independent of
  the key detector (share of sung notes inside the labelled set vs the best-fitting diatonic set)
  agrees within ~1-2 points on average (only 1/86 clips > 10 points worse), but melodies often fit a
  neighbouring set almost as well. Material is thin: few keys have >= 25-35 s per sex, so dev clips
  reuse songs and some key pairs; splices are song boundaries (new singer, timbre, level), not a
  modulation within one performance. The strict delay is harsh (any later chromatic frame resets it).
