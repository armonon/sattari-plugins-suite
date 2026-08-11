# Auto Pitch real-vocal qualification

Truth label: **internal validation only; source audio is not shipped or published**

Generated: 2026-08-10T18:13:39-07:00
Manifest: `/Users/lillypad/Projects/sattari-plugins-main/validation/corpus/auto-pitch.json`
Renderer: `/Users/lillypad/Projects/sattari-plugins-main/build/auto-pitch-neural/AutoPitchRender`

## Corpus gate

- Real cleared clips: **2 / 5 required**
- Required coverage tags: belting, fast_note_transitions, female_vocal, male_vocal, noisy_vocal, rap, rnb, soft_singing, vibrato
- Missing coverage tags: belting, female_vocal, male_vocal, rnb, soft_singing, vibrato
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

# Objective measurements (added after render)

Truth label: **first real-vocal evidence Auto Pitch has ever had. Corpus gate is
BLOCKED at 2/5 clips; nothing here is a listening result.**

## Output safety — independently measured, not the renderer's self-report

| render | length | peak | DC | finite |
|---|---|---:|---:|---|
| slay-demons-natural | matches input | 0.995 (-0.05 dBFS) | 2.6e-05 | yes |
| slay-demons-natural_hard | matches input | 0.992 (-0.07 dBFS) | -7.1e-05 | yes |
| guidos-pizza-natural | matches input | 0.994 (-0.05 dBFS) | -3.7e-05 | yes |
| guidos-pizza-natural_hard | matches input | 0.955 (-0.40 dBFS) | -4.3e-05 | yes |

No clipping, no NaN/Inf, no DC, no length drift. **PASS.**

Raw waveform correlation with the input is 0.04-0.18. That is NOT a damage
signal: a pitch shifter rewrites phase, so waveform correlation cannot judge
these outputs. The phase-insensitive measure is below.

## Spectral envelope preservation (mel, phase-insensitive)

| render | mel correlation | mean abs mel delta |
|---|---:|---:|
| slay-demons-natural | 0.787 | 7.6 dB |
| slay-demons-natural_hard | 0.788 | 8.1 dB |
| guidos-pizza-natural | 0.894 | 4.7 dB |
| guidos-pizza-natural_hard | 0.898 | 4.3 dB |

The rap clip is altered roughly twice as much as the band-limited clip.

## Pitch correction — and why the two clips must be read differently

Renderer self-report, distance to the nearest scale tone on sustained note
bodies:

| clip | key (AutoKey) | input | Natural | Hard |
|---|---|---:|---:|---:|
| slay-demons | C# minor | 46.5 ct | 51.0 ct (**-10%, worse**) | 45.5 ct (2%) |
| guidos-pizza | C# major | 28.4 ct | 13.6 ct (52%) | 11.2 ct (61%) |

### slay-demons — the metric is not valid here

50 cents is the maximum possible distance to a scale tone, and a pitch
distribution with no tonal intent yields ~50 ct BY CONSTRUCTION. This clip
measures 6.37 pitch changes per voiced second with a 0.89 s longest sustain: it
is rap, and rap has no intended pitch targets. An input reading of 46.5 ct is
therefore indistinguishable from "not tonal", and neither -10% nor +2% carries
information about correction quality.

An independent re-measurement over all confidently voiced frames (rather than
sustained bodies) disagreed with the renderer, which reinforces the point:

    INPUT  median 40.6 ct   mean 47.5 ct   (1221 confident voiced frames)
    HARD   median 30.6 ct   mean 44.8 ct   ( 433 confident voiced frames)

That looks like a 25% improvement but is not comparable: **the confidently
trackable frame count fell by 65%** after processing, so the two medians are
computed over different populations. Whether that frame loss is an artifact of
the processing or of measuring a rap vocal is UNRESOLVED and is the single most
useful thing to investigate next.

**Conclusion: cents-to-scale is the wrong metric for rap.** Rap clips should be
scored on artifact, consonant integrity and formant stability instead.

### guidos-pizza — the correction is real but the target is unverified

52-61% tightening is a strong result. It cannot yet be accepted, because the
reference pitch tracker FAILS on this clip:

- unambiguous harmonic comb at 176 / 352 / 527 Hz gives f0 ~176 Hz
- pyin with a 55 Hz floor reported 58.9 Hz — exactly f0/3
- at a 90 Hz floor it reported 88 Hz — exactly f0/2
- at 120 Hz it pins to 120 Hz, median voiced confidence 0.01

If Auto Pitch's own tracker makes the same sub-harmonic error, the plugin would
be tightening confidently toward a target an octave or a twelfth below the real
note, and the tightening percentage would still look excellent. **The number is
recorded; the correctness of the target is NOT VERIFIED.**

## Status

- Output safety: **PASS**
- Corpus gate: **BLOCKED** (2/5 clips; missing belting, female_vocal,
  male_vocal, rnb, soft_singing, vibrato)
- Pitch correction on tonal material: **NOT VERIFIED** (tracker unreliable on
  the only tonal clip)
- Pitch correction on rap: **metric invalid**; needs artifact-based scoring
- Human listening: **NOT VERIFIED**
