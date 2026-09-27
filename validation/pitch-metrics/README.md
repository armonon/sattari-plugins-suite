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

## Baseline (frozen cbd1ce4), N20EMv2 lead-vocal dev split (85 songs, 12,869 scored notes, 2026-09-27)

Classic style, rendered at 48 kHz from the 16 kHz sources (pitch metrics only).
- Within 5 / 10 / 20 c: 33.1 / 62.0 / 76.8%. Median 7.1 c, p95 50.9 c. Target semitone correct
  94.8%, 0 octave errors, 206 false corrections (1.6%).
- Notes with target flips: 9.9%. Legato settle: median 60 ms, p90 280 ms.
- Onsets (n = 10,182): shift -0.9 ms median; high-frequency attack -1.0 dB; rise time 19.1 -> 19.8 ms.
- Weakest segments:
  - Wide vibrato (> 40 c): 28.2% within 10 c, 45% of those notes flip target.
  - Sixth-or-wider leaps: 35% within 10 c; settle median 190 ms.
  - Low register: 54.5% within 10 c; settle p90 372 ms.
  - Long notes (> 400 ms) flip target 23% of the time.
- By sex (inferred from pitch, F / M): 65.9 / 58.9% within 10 c.
- Vibrato depth out/in: median 0.92 (p10 0.53, p90 1.45), i.e. flattened on some notes and
  exaggerated on others.

## 44.1 kHz defect found 2026-09-27 (release-blocking; under root-cause)

- In a real VST3 host (pedalboard, installed candidate), Studio mode at 44.1 kHz widens a +-40 c
  vibrato to -75 / +51 c (Classic) and -39 / +23 c (Natural).
- At 32, 48 and 96 kHz the same input comes out at +-17 to 29 c. Steady notes are perfect at every
  rate.
- AutoPitchBench, and every benchmark above (all rendered at 48 kHz), did not show it.
  AutoPitchLiveMatrix did. Diagnostic switches were added to both tools: `AP_DUMP_PARAMS`,
  `LM_VIB_FIRST`, `LM_NO_DRAIN`, `LM_NO_RATEDETAILS`, `LM_2CH`, `LM_NO_VR`, `LM_NO_XL`, `LM_NO_NP`.

### 44.1 kHz root-cause status (2026-09-27, afternoon)

- **Not host-only.** AutoPitchBench reproduces it once the input matches (a 0.18-peak tone,
  measured 1-3 s in). My first counter-test used a louder tone over a later window, which happened
  to be clean. The widening depends erratically on level and time at 44.1 kHz; 48 kHz is stable
  (+-20 c).
- **Not our control logic.** The requested correction per block is the same at 44.1 and 48 kHz:
  -0.91 x vibrato with a 12 ms lag, against -0.93 x with 11 ms; residual 2.5 / 2.1 c.
- **It is the Rubber Band R3 realtime render of that curve.** At 44.1 kHz the output envelope dips
  to 4% of its mean (48 kHz: 69%) and a spurious sideband appears at -14 dB (48 kHz: -32 dB). That is
  interference between components, which frequency trackers read as +-70 c "vibrato".
- **Standalone Rubber Band** (vendored 3.3 and brew 4.0, plugin options, a vibrato-cancelling
  curve) shows the same kind of rate-dependent irregularity. OptionWindowShort is involved: the
  standard window behaves smoothly. But the standard window costs 4616 samples of latency and
  breaks the plugin's correction timing at every rate, so it is not a fix.
- **Real singers (vocadito dev), native 44.1 vs 48 kHz:**
  - Within 10 c: 61.8 vs 62.0%. Target correct: 93.8 vs 93.6%.
  - Vibrato depth out/in p90: 1.64 vs 1.44.
  - Legato settle p90: 400 vs 294 ms.
  - Onset shift p90: +2.0 vs +0.7 ms.
  - Real material is affected mildly, in the tails; the pure-sine case is extreme.
- Tool bug fixed: AutoPitchBench appended to an existing --output file (JUCE createOutputStream
  appends), which produced "No RIFF chunk" files. It now deletes the file first.
