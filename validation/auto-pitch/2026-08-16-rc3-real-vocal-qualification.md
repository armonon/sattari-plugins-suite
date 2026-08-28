---
type: validation-evidence
product: Sattari Auto Pitch
version: 0.5.0 RC3
date: 2026-08-16
status: READY_FOR_USER_LISTENING
truth_label: internal local corpus; three source clips are not public-release assets
---

# RC3 real-vocal qualification

The final RC3 renderer processed all six cleared local clips and all nine required
coverage tags. Each source produced Natural and Hard renders using manual C
chromatic targeting, neural/YIN fusion, R3/Finer Short, full correction, and no
wrong-key AutoKey pull. Suite key-follow is qualified separately on full-mix
material because a solo vocal often cannot establish a global song key.

## Objective inventory

| Clip | Natural median tightening | Hard median tightening | Peak status |
|---|---:|---:|---|
| dickey-prairie | 55% | 68% | clean |
| guidos-pizza | 55% | 80% | clean |
| slay-demons | 46% | 64% | clean |
| vs-belt-m6 | 83% | 94% | clean |
| vs-melisma-m3 | 37% | 53% | clean |
| vs-vibrato-f5 | 37% | 25% | clean |

These numbers measure cents to the nearest chromatic semitone on pitch-stable
frames. They are useful on sustained/ordinary material but not a release metric
for the intentional-vibrato clip; that clip is supposed to cross the target. The
controlled modulation gate in `2026-08-16-rc3-fast-correction-hardening.md`
proves the separate Hard-flattening claim.

All 12 processed outputs are finite and unclipped. The three clips marked
`public_release_allowed: false` remain local and must not be uploaded or shipped.

## Blind package

Use only:

`~/Desktop/vocal test/listening-package-0.5.0-rc3/`

It contains 18 level-matched, 24-bit mono WAVs in six randomized sets, plus
`SCORING-SHEET.md`. Do not open `KEY-DO-NOT-OPEN.md` until every score and
timestamp note is complete. Earlier RC2 and 0.4.1 packages are superseded and
must not be mixed into this scoring pass.

## Boundary

The corpus gate is ready; the listening gate remains open. Only the user may
score musicality, artifacts, timing, consonants, timbre, and preference. No score
has been inferred or fabricated here.
