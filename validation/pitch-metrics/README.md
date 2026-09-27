# Objective pitch metrics (Track B), owner spec 2026-09-27 Part I sections 3, 6, 7 and 8

`pitch_metrics.py` scores the output against annotated notes and reports every metric per segment:
register, note length, vibrato depth, transition interval and direction. The default render binary
is the frozen Track A candidate (read-only). Renders are deleted after scoring.

## Baseline (frozen cbd1ce4), vocadito dev (26 tracks, 716 agreed notes), 2026-09-27

| | Classic | Natural |
|---|---|---|
| within 5 / 10 / 20 c | 33.5 / 62.0 / 77.1% | 34.9 / 60.5 / 76.6% |
| median / p95 | 7.4 / 54.9 c | 7.1 / 55.5 c |
| target semitone correct | 93.6% | 93.2% |
| octave errors / false corrections | 0 / 12 (1.7%) | 1 / 12 |
| notes with target flips (200 ms smoothed) | 3.8% (long notes 11%, vibrato > 40 c: 23%) | same |
| legato settle, median / p90 | 60 / 294 ms | 60 / 250 ms |
| vibrato depth out/in, median (p10-p90) | 0.91 (0.57-1.44) | 0.89 (0.53-1.37) |
| onsets (n=470): shift, 2-8 kHz attack, rise time | -0.8 ms, -1.2 dB, 17.6 -> 18.0 ms | -0.8 ms, -1.2 dB, 17.6 -> 19.0 ms |

Findings:
- **Consonants and onsets on real vocals are not delayed.** The median shift is -0.8 ms (p10-p90:
  -1.8 to +0.7 ms) and the high-frequency attack drops 1.2 dB. The 7-10 ms "late transients" seen on
  synthetic clicks do not show on real singing. Blind listening must still confirm this.
- **Wide vibrato is the weak segment**: 38% within 10 c, and 23% of those notes flip target.
- **Vibrato depth is not preserved consistently.** It is sometimes flattened (0.57x) and sometimes
  exaggerated (1.44x). This is the next thing to study for Natural mode.
- The target is right for 93% of notes. Most residual error is centring (median 7 c), not wrong
  notes.
- Caveats: vocadito is small, the singers are untrained, and the recordings are phone quality. The
  scorer (CREPE-tiny) is the same model for input and output. The register split is by pitch only
  (vocadito has no sex labels). The lead-vocal corpus (validation/leadvocal/) will replace this set.
