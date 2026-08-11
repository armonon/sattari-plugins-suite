# Auto Pitch real-vocal qualification

Truth label: **internal validation only; source audio is not shipped or published**

Generated: 2026-08-10T18:39:56-07:00
Manifest: `/Users/lillypad/Projects/sattari-plugins-main/validation/corpus/auto-pitch.json`
Renderer: `/Users/lillypad/Projects/sattari-plugins-main/build/auto-pitch-neural/AutoPitchRender`

## Corpus gate

- Real cleared clips: **3 / 5 required**
- Required coverage tags: belting, fast_note_transitions, female_vocal, male_vocal, noisy_vocal, rap, rnb, soft_singing, vibrato
- Missing coverage tags: belting, female_vocal, male_vocal, rnb, vibrato
- Corpus gate: **BLOCKED**

Synthetic fixtures do not count toward this gate. A ready corpus is not a listening
pass; a human must score every rendered pair below on level-matched monitoring.

## Render inventory and listening scores

Scores use 1 = unacceptable, 3 = usable with caveats, 5 = excellent. Artifact
severity is reversed: 1 = severe, 5 = inaudible.

| ID | Tags | Permission | Source SHA-256 | Render | Musicality | Artifacts | Timing | Decision |
|---|---|---|---|---|---:|---:|---:|---|
| slay-demons | rap, fast_note_transitions | owned | `089ba61636450f578e997b74d59bff69a0c253a6ad7431801c7b3b3aff4f5ff6` | rendered | TBD | TBD | TBD | hold |
| guidos-pizza | noisy_vocal | owned | `c3a113daa19fc81da86bd3642198e08f6b53daaf7b5df5014b8102e5176514a3` | rendered | TBD | TBD | TBD | hold |
| dickey-prairie | soft_singing, noisy_vocal | owned | `d960779141d9b10e2df89544c92bd9a9de19b3492feaddaa3186540d4ae01b90` | rendered | TBD | TBD | TBD | hold |

## Required listening checks

- [ ] Natural correction centers sustained notes without flattening intentional vibrato.
- [ ] Hard correction reaches the target without crackle or block-boundary modulation.
- [ ] Plosives, sibilants, breaths, and unvoiced consonants remain natural.
- [ ] Formants remain stable through octave errors, belts, and low-register vocals.
- [ ] Fast note changes do not lag, overshoot, or snap to a transient.
- [ ] Offline bounce matches realtime behavior within the documented latency mode.
- [ ] Output is level-matched and contains no clipping, NaN, silence, or dropouts.

## Verdict

- [ ] Hold
- [ ] Promote to validated alpha
- [ ] Promote to release candidate

Rationale:

---

# Addendum — third clip (dickey-prairie) and an AutoKey discrepancy

## dickey-prairie is the first credible tonal evidence Auto Pitch has

The earlier two clips could not settle the question: one is rap (no pitch
targets) and one defeats the reference tracker. This clip is sustained melodic
singing that our independent tracker follows at confidence 0.48 — four times
guidos-pizza's 0.01 and nearly double slay-demons' 0.28.

| | renderer self-report | independent re-measurement |
|---|---:|---:|
| input | 23.4 ct | 20.6 ct |
| Natural | 12.8 ct (45%) | 10.6 ct (48%) |
| Hard | 10.0 ct (57%) | 9.4 ct (55%) |

**Two independent measurements agree within 3 percentage points.** Confidently
trackable frames fell 938 -> 696/736, a 22-26% loss, against the 65% loss seen
on the rap clip — which suggests that loss was rap-specific rather than a
general processing artifact.

The result is also robust to the key disagreement below: C# major and G# major
share six of seven scale tones, so cents-to-scale barely moves between them.

Output peak was 0.030 (natural) / 0.034 (hard) — the renderer correctly passed
a quiet source through without auto-gain. Any listening test on this clip must
be level-matched.

## AUTOKEY DISAGREES WITH AN INDEPENDENT ESTIMATE ON ALL THREE CLIPS

| clip | AutoKey | Krumhansl-Schmuckler on chroma | fit |
|---|---|---|---:|
| slay-demons | C# minor | F minor | r=0.29 (weak) |
| guidos-pizza | C# major | A minor | r=0.65 |
| dickey-prairie | C# major | **G# major** | **r=0.83 (strong)** |

Two things stand out. AutoKey returned **C# for three unrelated recordings**,
and on the one clip where the independent estimator is confident (r=0.83) they
disagree by a fifth — G# is the dominant of C#, which is a classic key-detection
confusion rather than a random error.

This is NOT yet a verdict against AutoKey. Krumhansl-Schmuckler on a solo
monophonic vocal is itself weak evidence: chroma from a single melodic line
without accompanying harmony is thin, which is exactly why the rap clip scores
r=0.29 for both. Neither estimator is ground truth here.

What it does establish is that **a Tier A component returned the same tonic for
three unrelated recordings and cannot currently be corroborated**. That warrants
investigation with proper ground truth before any key-dependent claim is made.

## Coverage after three clips

- Real cleared clips: **3 / 5**
- Covered: rap, fast_note_transitions, noisy_vocal, soft_singing
- Missing: belting, female_vocal, male_vocal, rnb, vibrato
- Corpus gate: **BLOCKED**
