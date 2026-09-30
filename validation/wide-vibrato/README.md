# Wide vibrato (Track B perceptual project 1), 2026-09-29

Owner spec 2026-09-27: wide vibrato is the first perceptual project, and "do not simply smooth harder".
All numbers here are from the R&D build, which includes the c63d63c release fix, at 48 kHz, 512-sample
blocks, C chromatic, Amount 1, Speed 1.

## Tools

- `wv_synth.py [tag] [bench args]`: a controlled grid. A vocal-like harmonic tone with sinusoidal
  vibrato is rendered through AutoPitchBench, varying:
  - depth: 30 / 50 / 70 / 100 c (half peak-to-peak)
  - rate: 5 / 6.5 Hz
  - centre offset from a semitone: 0 / 20 / 35 / 45 c
  - style: Natural / Classic

  It reports, per cell:
  - output/input depth ratio
  - |output centre - nearest semitone|
  - target changes per second, read from the plugin's own chosen target (trace `targetMidi`)

  Pitch is scored with CREPE-full, the same scorer as the real-singer metrics. Two measurement
  traps were caught on the way and are fixed here:
  - A Hilbert instantaneous frequency is biased by up to 11 c on Rubber Band output.
  - The trace field `detectedMidi` is only the nearest note of the INPUT, not the target. The
    bench now records `targetMidi`, `appliedCents`, `inputMidi` and the vibrato-target state.
- The real-singer segment analysis is in the session notes: sustained, audio-confirmed segments of
  N20EM dev with the plugin's own targets. N20EM is research data only.

## Findings

**1. The mechanism: target flipping.**
- With a vibrato of 50 c or more whose centre sits 20-45 c off a semitone, the current target
  hysteresis flips the target at every vibrato half-cycle, 11.4 times a second. The hysteresis
  switches once the pitch is 25 c past the midpoint, or has been nearer the other note for 120 ms.
- Whenever it flips, the note centre is not corrected at all: the output centre error equals the
  input offset, 32-49 c.
- The vibrato is not reduced (ratio about 1.0) and is sometimes widened, up to 1.39x in Classic.
- Without flips the centre lands within 5 c and depth drops to about 0.55-0.75x.

**2. Real singers flip too, and often.** On audio-confirmed sustained segments (at least 400 ms,
voiced throughout, within ±1 st of the segment median; N20EM dev, 1/6 shard, plugin's own targets):

| segment type | segments that flip target |
|---|---|
| wide vibrato (> 40 c) | 93-95%; 100% when the singer's centre is 25 c or more off a note |
| moderate vibrato (15-40 c) | 60% |
| other moving segments | 66% |

The detector is not the cause: its per-block pitch is off by more than 50 c on only 1-4% of frames
against CREPE. This is the likely main source of audible warble on held notes.

**3. First fix: a vibrato-centre target** (`setVibratoTargetEnabled`, bench `--vibrato-target`;
default OFF).
- While a regular, sine-like oscillation is detected, the target is chosen and held from its centre,
  the mean over one measured period. The oscillation must be 3.3-10 Hz, 20-160 c, at least 2
  cycles, with regular half-periods and an RMS/extent between 0.50 and 0.80.
- **Synthetic grid: it fixes the defect.**
  - Flips drop to 0.
  - The centre error at offset 35 c / depth 50 c goes from 35-37 c to 5 c.
  - At offset 45 c / depth 100 c it goes from 48-49 c to 24-25 c.
  - The vibrato is never widened: ratio 0.54-0.85x in Classic, one Natural cell 1.03x.
- **Real singers: almost no effect.** It engages on only about 1% of frames in real wide-vibrato
  notes. Real notes are short, and real vibrato is irregular.
- A looser first version did engage more, but it treated runs of short alternating notes as
  vibrato. It made 50 notes worse against 39 better, and produced one octave-scored note.
- The strict version is neutral in Classic (1 note worse, 3 better, 696 of 716 unchanged on
  vocadito dev). In Natural it is slightly negative (10 worse, 1 better).
- N20EM dev, full 12,869 notes, Classic: no measurable change.
- It stays default-off until it earns its place on real voices.

**4. Simple hysteresis retuning does not fix real voices.** These were simulated offline on the
plugin's real per-block pitch; the simulator reproduces the plugin's flip rates exactly:
- margin persistence
- smoothed decision pitch
- longer switch timers
- a "note-lock" with fast-median, slow-mean and leap paths

None beat the current rule on real sustained segments (wide 82-100%) without slowing legato note
changes. Two things defeat them:
- Real wide vibrato dwells near the adjacent semitone at its peaks.
- Onsets start off-note.

## Next

- Score target stability by wrong-target frames per sustained segment (target ≠ the segment's
  median note), not only by "any change". The initial switch after a scooped onset is correct
  behaviour, and counting it as a flip penalises every lock design.
- Design candidates:
  - A note-level centre estimate that learns the singer's centre and extent since the note onset.
    Hold while the pitch stays inside centre ± (extent + margin), and leave only on a sustained
    departure.
  - Lookahead from the existing Studio latency budget, if any part of the 66 ms is available to
    the analysis without adding latency. That needs checking against the engine timing first.
- Every candidate is judged on real sustained segments, legato switch delay and the synthetic grid,
  and must be neutral on steady notes and the golden. The sealed holdout is never used.
