# Key Estimator 2.0: corpus plan (documentation only; the project starts after release qualification)

Rules (owner, 2026-09-25):
- Real lead vocals are the primary benchmark. Choir parts are a secondary, adversarial set.
- Development and holdout sets are split by singer and song. Holdout stays sealed and is opened
  only for milestone validation. Nothing is tuned on it.
- Every corpus item carries its source and licence in the manifest.
- Non-commercial sets are for internal research only, when their licence explicitly permits it.
  They stay out of shipping assets and are never the sole evidence for a product claim.
- The owner's cleared stems are the most important corpus.

## Sources we already have

| source | licence / use | what it covers | gaps |
|---|---|---|---|
| Owner's cleared stems (`validation/corpus/auto-pitch.json`, on the lillypad Mac) | owned / cleared, internal eval | the actual target product: raw lead takes, rap, melismatic, belting, soft, noisy | on the other machine only; key labels missing (ask the owner); size unknown |
| Apple Loops vocals (`al_vocals.py`, 282 songs / 970 phrases, 93 min) | Logic Pro licence: internal evaluation only, never redistributed | male/female lead phrases, Apple key labels, several genres | mostly ~6 s single phrases; may be pre-tuned; labels unverified; no backing bleed; no real within-song modulation |
| vocadito (`~/datasets/vocadito`, 40 excerpts) | CC BY 4.0 | untrained singers, phone quality, 7 languages, note annotations (2 annotators) | small; no key labels (derivable from notes); several excerpts are not major/minor |
| Choral Singing Dataset (48 tracks) | CC BY 4.0 | choir parts with scores (keys known) | not lead vocals: secondary, adversarial set |
| `key_change_real.py` (86 spliced clips) | inherits Apple Loops + CSD | modulation delay, false switches, chatter | splices are song boundaries, not modulations within one performance |

## N20EMv2: what it would add (decision pending; not downloaded)

- Licence: CC BY-SA 4.0 (zenodo.org/records/10814703). Internal R&D use is permitted.
  Share-alike applies only if we redistribute the audio or derivatives; we would not.
- It adds, over what we have:
  - Whole-song lead vocals: 157 English pop covers, 8 h 22 min, 30 singers (17 male, 13 female),
    untrained to amateur. This is the "sparse verse / dense chorus / long song" material we lack.
    Apple Loops are ~6 s phrases, and vocadito excerpts are short.
  - Expert-corrected note annotations, from which each song's key (and any modulation) can be
    derived. That makes it a real lead-vocal key benchmark with time-varying truth, which none of
    our current sets provide.
  - Enough singers for a proper singer-split holdout (for example, 8 of the 30 sealed).
- It does not add: backing bleed (vocals are isolated), studio-quality audio (the source set was
  downsampled to 16 kHz, to be confirmed for v2), or genre breadth beyond English pop covers.
- Cost: 11.5 GB download and roughly the same again if converted to 48 kHz. 19 GB is free on the
  dev Mac today, so it needs cleanup first or external storage.
- Recommendation: download when Key Estimator 2.0 starts, not before. It is the only cleared
  source of whole-song lead vocals with note truth.

## Other candidates (from the 2026-09-25 licence survey)

- **Children's Song Dataset** (CC BY-NC-SA 4.0): the best key-labelled set, since each song is
  sung in two keys with MIDI. Non-commercial, so internal research only, with owner sign-off.
- **ccMixter a cappellas:** 1,483 CC BY + 24 public domain. Real pop, R&B and hip-hop leads, some
  with bleed. No labels; keys would have to be annotated by us. Attribution list required.
- **Freesound CC0 / CC BY a cappellas:** mixed quality, needs an account.
- **MedleyDB / MUSDB18 / Cambridge-MT / Opencpop / M4Singer / GTSinger:** non-commercial or
  educational licences. Internal research only with owner sign-off; never used as product evidence.

## Manifest schema (every item)

`id, source, licence, licence_url, use (internal-eval | internal-research-NC), path, singer_id,
song_id, sex, register, genre, duration_s, key_truth (+ time-varying segments), key_truth_method
(label | derived-from-notes | annotated), has_bleed, notes_available, split (dev | holdout-sealed)`
