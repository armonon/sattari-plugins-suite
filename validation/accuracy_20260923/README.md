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
