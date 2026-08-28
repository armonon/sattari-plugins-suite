---
type: validation-evidence
product: shared KeyAnalysisEngine / Sattari Compass
date: 2026-08-16
status: PASS
truth_label: implementation contract only; not an accuracy or lifecycle claim
---

# KeyAnalysisEngine contract

## Result

**PASS.** `SattariKeyAnalysisContract` now locks the shared engine's routing and
fallback semantics without changing Compass or any other plugin.

The contract proves:

- `KeyAnalysisConfig{}` remains Aarden + chroma-only.
- Default-engine decisions are bit-identical to `detectKey()` for all 12
  transpositions in both major and minor, including the confidence value used by
  publication gates.
- All six runtime-selectable profile pairs route through the expected profile
  tables and the same decision core.
- Silence and empty input remain invalid and report `KeyAnalysisSource::None`.
- A requested but unavailable neural model falls back to a valid chroma result
  and reports `KeyAnalysisSource::Chroma`.
- Repeated whole-buffer analysis is deterministic.

The target deliberately compiles with `SATTARI_HAS_ONNX=0`. That is the contract
being tested: neural inference may enhance the engine, but it cannot be
load-bearing for basic key analysis.

## Evidence

Focused build and test:

```text
cmake --build build/local --target SattariKeyAnalysisContract -j 4
ctest --test-dir build/local -R '^SattariKeyAnalysisContract$' --output-on-failure

1/1 Test #24: SattariKeyAnalysisContract ....... Passed
100% tests passed, 0 tests failed out of 1
```

Full locally available suite, explicitly excluding StemDeck, after final RC3
Auto Pitch adoption and the original Sattari Key routing work:

```text
ctest --test-dir build/local -L non-stemdeck --output-on-failure -j 4

100% tests passed, 0 tests failed out of 97
Total Test time (real) = 49.18 sec
```

GitHub Actions was not used. The standing account-level billing block remains
unchanged, so local verification is authoritative for this work.

## Scope boundary

This closes the **shared engine implementation contract** item and RC3 adoption
by Auto Pitch. Sattari Key retains this engine only as a clean-tonal live fallback;
its full-song verdict now uses the separately validated balanced full-mix engine.
This contract does not validate real-world key accuracy,
calibrate confidence across genres, resolve the three-publishers/one-bus policy,
or promote Compass beyond alpha. The held-out cross-genre corpus remains the
accuracy gate.

The earlier Auto Pitch 0.4.1 candidate remains preserved at `7fef544a6d49`.
Auto Pitch 0.5.0 RC3 is a separate working line created before any listening
scores existed; it consumes this shared engine and quarantines the failed neural
key override.
