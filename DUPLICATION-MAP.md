# Sattari — DSP Duplication Map (Phase 0)

Where DSP is **duplicated across plugins** vs already **shared** in `core/include/sattari_audio_core/`.
This is the input to Phase 2 (extract shared engines). Ranked by *duplication × value*.

## Already shared (the good foundation)
These core headers are consumed by many plugins and are the model to follow:
`SattariPitchDetector.h` (YIN), `SattariCrepePitch.h` (ONNX neural pitch), `SattariEnvelopeFollower.h`,
`SattariPsolaShifter.h`, `SattariChromagram.h`, `SattariKeyDetector.h` (Krumhansl), `SattariGranularEngine.h`,
`SattariStemSeparator.h`, `SattariKeyButton.h`, `SattariLookAndFeel.h`, `SattariCrossPluginBus.h`,
`SattariPresetManager.h`, `SattariMidiLearn.h`, `SattariRealtime.h`, `SattariRenderHarness.h`.

## Duplicated (extract these)

| Primitive | Own private copy in… | # | Uses shared | Extract → |
|---|---|---:|---|---|
| **Biquad / filter** | bus, deess, eq, glass-choir, heat, imager, meter, peak, vocoder, warp | **10** | (none — all private) | `SattariFilter.h` (RBJ biquads, LR crossovers, one-pole) |
| **Pitch / YIN** | auto-pitch, voxkey, stemdeck (private) | 3 | tuner uses shared | fold all onto `SattariPitchDetector.h` (voxkey has a full YIN duplicate) |
| **FFT / STFT spectral** | auto-pitch, scope, song-key, voxkey | 4 | partly via `SattariChromagram.h` | `SattariSpectral.h` (windowed STFT + chroma) |
| **RbFormant** (RubberBand formant shift) | face, glass-choir (verbatim copies) | 2 | harmony rolls its own RB voicing | `SattariRbFormant.h` (core) |
| **Saturation / waveshaper** | bus, royal-chain, heat, warp (each own tanh/ADAA) | 4 | — | `SattariSaturator.h` (ADAA + oversampled) |
| **Reverb wrapper** (juce::Reverb pure-wet trick) | space, shimmer, granulator, glass-choir | 4 | — | `SattariReverb.h` (the dry×2 cancel pattern) |

## Ranked extraction plan (Phase 2)

1. **`SattariFilter.h` — biquad/filter engine.** *Highest duplication (10 plugins).* Every EQ/de-ess/imager/meter
   re-implements RBJ biquads and LR crossovers. One tested filter lib de-risks the whole Mix product and
   Vocal tone stages. Low regression risk (math is standard); big surface-area win.
2. **Pitch engine consolidation.** *Highest value.* Brain + Vocal + Composer all depend on pitch. VoxKey's private
   YIN duplicate is drift-prone (flagged in review). Unify auto-pitch/voxkey/stemdeck onto `SattariPitchDetector.h`
   (+ `SattariCrepePitch.h`). Gate: improving the shared detector measurably improves Auto Pitch **and** Harmony/VoxKey at once.
3. **`SattariRbFormant.h` — formant-preserving shift.** *Medium count, high Vocal value.* face + glass-choir carry
   byte-identical copies; harmony has a parallel RB path. One formant engine powers Doubler/Face/Glass/Harmony.

Runner-up: **`SattariSpectral.h`** (STFT unify: auto-pitch/scope/song-key/voxkey) and **`SattariSaturator.h`**
(bus/royal-chain/heat/warp) — both worth doing once the top 3 land.

## Rules (per working agreement)
- Extract with **semver'd internal APIs + tests BEFORE deleting** any duplicate copy.
- Prove each extraction improves **≥2 products** with an A/B (the Phase-2 gate).
- Do **not** block the 40→6 repackage on any of this — repackage ships first (presentation only).
