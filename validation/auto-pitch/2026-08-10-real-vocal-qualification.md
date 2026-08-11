# Auto Pitch real-vocal qualification

Truth label: **internal validation only; source audio is not shipped or published**

Generated: 2026-08-11T11:45:20-07:00
Manifest: `/Users/lillypad/Projects/sattari-plugins-main/validation/corpus/auto-pitch.json`
Renderer: `/Users/lillypad/Projects/sattari-plugins-main/build/auto-pitch-neural/AutoPitchRender`

## Corpus gate

- Real cleared clips: **6 / 5 required**
- Required coverage tags: belting, fast_note_transitions, female_vocal, male_vocal, melismatic, noisy_vocal, rap, soft_singing, vibrato
- Missing coverage tags: none
- Corpus gate: **READY FOR LISTENING**

Synthetic fixtures do not count toward this gate. A ready corpus is not a listening
pass; a human must score every rendered pair below on level-matched monitoring.

## Render inventory and listening scores

Scores use 1 = unacceptable, 3 = usable with caveats, 5 = excellent. Artifact
severity is reversed: 1 = severe, 5 = inaudible.

| ID | Tags | Permission | Source SHA-256 | Render | Musicality | Artifacts | Timing | Decision |
|---|---|---|---|---|---:|---:|---:|---|
| slay-demons | rap, fast_note_transitions | owned | `089ba61636450f578e997b74d59bff69a0c253a6ad7431801c7b3b3aff4f5ff6` | rendered | TBD | TBD | TBD | hold |
| guidos-pizza | noisy_vocal | owned | `c3a113daa19fc81da86bd3642198e08f6b53daaf7b5df5014b8102e5176514a3` | rendered | TBD | TBD | TBD | hold |
| dickey-prairie | soft_singing, noisy_vocal, female_vocal | owned | `d960779141d9b10e2df89544c92bd9a9de19b3492feaddaa3186540d4ae01b90` | rendered | TBD | TBD | TBD | hold |
| vs-belt-m6 | belting, male_vocal | public-domain | `899f08dfdcef6c18d0f143e4d56d633cb3deede02e7269101636df15c2917e5a` | rendered | TBD | TBD | TBD | hold |
| vs-vibrato-f5 | vibrato, female_vocal | public-domain | `e180daef389554fd4b8d46ab143c92a18bf45220dd2e4645f4d3efa1a4fb2736` | rendered | TBD | TBD | TBD | hold |
| vs-melisma-m3 | melismatic, male_vocal | public-domain | `e5dc3085841f5839da70c0d685bcf3c19675514c33b188a16e75f852d2d648d5` | rendered | TBD | TBD | TBD | hold |

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
