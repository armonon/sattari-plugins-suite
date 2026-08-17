---
type: real-vocal-qualification
product: auto-pitch
candidate_revision: Auto Pitch 0.5.0 RC2 working tree based on 767becadb7cd
date: 2026-08-16
truth_label: corpus/render safety passed; listening not yet scored
status: READY FOR USER LISTENING
---

# Auto Pitch 0.5.0 RC2 — real-vocal qualification

The RC2 renderer processed the six-clip local corpus into Natural and Natural
Hard candidates. Source audio and rendered audio remain outside git. The three
clips marked `public_release_allowed: false` were processed locally only and
were not uploaded or published.

## Corpus gate

- Cleared clips: **6 / 5 required**
- Required tags: **9 / 9 covered**
- Tags: belting, fast note transitions, female vocal, male vocal, melismatic,
  noisy vocal, rap, soft singing, vibrato
- Candidate renders: **12** (two per clip)
- Blind package: **18 WAV files** (source, Natural, Natural Hard per set)
- Output: `/Users/lillypad/Desktop/vocal test/listening-package-rc2`
- Format check: mono 24-bit PCM WAV; per-set active RMS matched; shared safety
  gain keeps the loudest item at -1 dBFS
- Overwrite guard: verified; the generator refuses a non-empty package without
  explicit `--force`
- Original package: preserved at
  `/Users/lillypad/Desktop/vocal test/listening-package`

## Listening boundary

The package key has not been opened. Scores remain `TBD`; no quality or parity
result may be inferred from successful rendering. The user must complete every
row in `SCORING-SHEET.md` before opening `KEY-DO-NOT-OPEN.md`.

The listening session should specifically record timestamps for metallic
texture, consonant smearing, formant drift, wrong-note pulls, flattened vibrato,
transition lag, crackles and dropouts. Objective DSP tests cannot substitute for
those judgments.
