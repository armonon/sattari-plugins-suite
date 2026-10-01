# Polyphonic instrumental correction

Poly is Auto Pitch's per-peak phase-vocoder mode for sustained tonal material.
It is different from **Beat / Sample Transpose**, which moves a complete signal
by one fixed interval. Poly does not provide source separation or independent
graph objects for every note in a dense mix.

## Controls and routing

- Amount controls correction strength. Manual key, Follow Song Key, key maps,
  reference tuning, and Edit Notes supply the tuning grid.
- A target must be both in the selected scale and enabled in Edit Notes.
  The legacy all-notes-disabled value (mask zero) means use the scale without
  additional exclusions. A nonzero mask with no in-scale notes leaves correction
  at unity and suppresses generated harmony; it must not invent a forbidden note.
- High Tracking extends the tracked high-frequency band from 2.5 to 7 kHz.
- Tonal Focus routes predominantly percussive bins through unchanged and excludes
  those same bins from Poly Harmony.
- Poly Harmony adds the in-key third where the enabled-note grid permits it.
- Retune Speed, Humanize, Flex Tune, Do No Harm, mono formant controls, and mono
  MIDI targeting do not control this renderer. Their UI controls are disabled
  while Poly runs; their saved/automated values are preserved for returning to mono.
  **Amount remains enabled** (its historical editor member name is `flexTune`).
- The separate four-voice vocal harmony renderer does not run over Poly.
- Whole-mix transpose takes precedence over Poly; Poly takes precedence over Live.
  The header identifies the active engine, even if an inactive Live setting remains on.

## Latency

The current 4096-point streaming implementation adds **4096 samples**: 3072 input
FIFO samples plus one 1024-sample output hop. Its host report must include both.
Enabling graph time correction adds its separate delay.

| Sample rate | Poly delay, excluding interface/DAW buffering |
|---|---|
| 44.1 kHz | 92.88 ms |
| 48 kHz | 85.33 ms |
| 96 kHz | 42.67 ms |

This mode is for production, not ultra-low-latency live monitoring. Correct delay
compensation aligns recorded playback; it does not remove monitoring latency.

## Harmonic grouping and bass

Persistent peaks are matched across adjacent bins. A lower fundamental can own
an upper partial only when at least two distinct upper harmonics corroborate its
comb over three frames and their frequencies agree closely with integer ratios.
An isolated, independently detuned octave is not sufficient ownership evidence.
This protects the reproduced +/-10 and +/-20-cent octave cases while retaining
harmonic grouping for actual rich tones.

Peak search includes lower bass bins rather than starting at 60 Hz. Resolution is
still limited by the fixed FFT size and sample rate. Resolving two low notes whose
partials overlap is a different problem from correcting one isolated bass sine.
No claim of arbitrary sub-bass or dense-mix separation follows from that coverage.

## Regression gate

```sh
cmake --build build-dynamic-key --target AutoPitchCoreContract --parallel 2
ctest --test-dir build-dynamic-key -R '^AutoPitchPolyphonicContract$' --output-on-failure
```

This gate exercises the production processor and checks scale exclusions,
4096-sample delay at multiple rates/block sizes, small-detune octaves, isolated
bass, harmonic-rich tones/chords, percussive harmony suppression, proportional
stereo, state/block invariance, and enabled UI controls. Its pure target-selector
checks enumerate every 12-bit pitch-class mask.

Passing synthetic tests is not a listening verdict or host qualification.
Representative instruments, reverb, dense stereo arrangements, automation,
transport changes, and long sessions still require broader evaluation. Old
`validation/poly_tune` Phase A/B scores evaluate offline detector prototypes,
not this real-time renderer's sound quality.
