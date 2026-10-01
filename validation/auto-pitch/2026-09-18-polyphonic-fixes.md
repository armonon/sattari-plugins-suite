# Polyphonic tuner repair verification

Base commit: `32b7c09`. Results apply to the subsequent local polyphonic repair
changes, not to the already-installed plugin or historical release gates.

## Changes

- Report the full 4096-sample streaming delay and size dry-reference storage to
  accommodate it. Do not change the actual FFT delay or claim lower latency.
- Select the nearest note in the scale/user-mask intersection. Empty intersections
  pass through and do not generate a forbidden harmony.
- Replace broad greedy harmonic ownership with persistent peak matching, distinct
  harmonic-support evidence, and tighter frequency agreement. Independently
  detuned octave tones no longer inherit each other's correction in the fixtures.
- Include lower bass bins instead of excluding all fundamentals below 60 Hz.
- Apply Tonal Focus routing to Poly Harmony as well as main correction.
- Disable unsupported mono controls in Poly while retaining Amount and poly
  controls; preserve their parameter values for returning to mono. Show POLY or
  TRANSPOSE in the header rather than a dormant Live mode's name.

## Build and tests

`AutoPitchCoreContract` and the production shared-code target built successfully
with the existing Release configuration (Rubber Band enabled; experimental
Signalsmith/Elastique/ONNX unavailable/disabled). The executable contains arm64
and x86_64 slices; these executions were native arm64, not an Intel host test.

```sh
cmake --build build-dynamic-key --target AutoPitchCoreContract --parallel 2
ctest --test-dir build-dynamic-key \
  -R '^AutoPitch(PolyphonicContract|CoreContract|ArrangementClipSmoke|SourceMatchSmoke)$' \
  --output-on-failure -V
```

All four tests passed:

| Gate | Result |
|---|---|
| AutoPitchCoreContract | PASS; includes factory reset, graph editing, live targets, harmony, live profiles, dynamic-key following |
| AutoPitchPolyphonicContract | PASS; 117 checks, 52 production-processor renders |
| AutoPitchArrangementClipSmoke | PASS |
| AutoPitchSourceMatchSmoke | PASS |

## Poly results

- **Delay:** reported = measured = 4096 samples at 44.1/48/96 kHz, each with
  64/257/512-sample host blocks (nine combinations).
- **Note masks:** every 12-bit allowed mask checked against a nearest-note oracle;
  actual excluded-third renders passed in all twelve major roots. The previously
  out-of-key E-flat output is replaced by legal F in C major.
- **Octaves:** independently detuned C4/C5 at opposing 10, 20 and 35 cents corrected
  to 0/0 cents at all three tested sample rates (2-cent spectral-search grid).
- **Bass:** isolated E1/A1/E2 corrected within 0–2 cents across those rates; the
  2-cent result was E1 at 96 kHz.
- **Harmonic retention:** rich single-note second/third/fifth harmonics passed
  their corrected-fundamental integer-ratio tests.
- **Existing chord fixtures:** all five rich opposing-detune chord cases passed;
  worst-note corrected/original two-frequency magnitude scores were
  0.945, 0.949, 0.952, 0.950, 0.949. These are not general accuracy percentages.
- **Percussive harmony:** protected/unprotected added-harmony energy ratio printed
  0.000000 on the deterministic percussion-only fixture (threshold <0.1).
- **Stereo/block/recall:** proportional-stereo maximum sample error
  `1.564622e-08`; block-size/state-recall maximum difference `0` on the tested tone.
- **UI:** unsupported mono controls disabled, Amount/poly controls enabled,
  actual Poly mode identified despite saved Live setting, controls restored on
  leaving Poly.
- All rendered fixtures passed finite/output-ceiling checks.

The permanent test is `plugins/auto-pitch/tests/PolyphonicContract.h`, invoked via
`AutoPitchCoreContract --poly-contract` and registered with CTest. Failures return
a nonzero exit code; this is no longer a temporary observational-only harness.

## Limits

Synthetic regression results do not establish transparent sound on arbitrary
instrumentals, source separation, dense-bass resolution, stereo coherence for
every arrangement, or host qualification. Poly still adds 85.33 ms at 48 kHz
before interface/DAW buffering. Human listening and real-host evaluation remain
necessary; no listening scores were supplied or invented.

No private audio was used or uploaded. No plugin bundle was installed or signed,
no notarization or StemDeck work occurred, and these changes were not committed
or pushed as part of this repair turn.
