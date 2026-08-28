---
type: accuracy-measurement
subject: SattariSongKeyEstimator vs chroma profiles
date: 2026-08-18
truth_label: internal validation only; nothing here is a shipping claim
status: LARGEST VERIFIED KEY IMPROVEMENT IN THIS REPOSITORY
---

# The new Song Key engine, measured against ground truth

The 26-file TuneBat comparison recorded 26/26 agreement and said plainly that the
figure "must not be called held-out performance", because that folder was used
while tuning the tempo selection. Correct, and it left the real question open:
does the engine generalise, or is it fitted to 26 related works?

It generalises. `SattariKeyDatasetEval` scores it beside every chroma profile on
the two labelled corpora, which have expert key labels and were **not** used to
tune it.

| detector | GiantSteps 500 (EDM) | combined 1337 (9 genres + EDM) |
|---|---:|---:|
| Aarden-Essen — what Compass ran | 39.8% / 47.34 | 39.2% / 46.69 |
| Krumhansl-Schmuckler | 38.8% / 51.90 | 40.5% / 52.10 |
| Temperley/Kostka-Payne | 39.0% / 48.86 | 41.7% / 49.45 |
| Bellman-Budge | 37.6% / 44.84 | 40.2% / 46.38 |
| Sha'ath — previous best | 45.2% / 56.56 | 44.2% / 54.55 |
| **Sattari Song Key** | **55.6% / 63.24** | **57.6% / 65.12** |

Raw exact % / MIREX x100.

**+13.4 points of exact key and +10.57 MIREX** over the best profile, across 1337
labelled tracks in ten genres.

## Why this one counts and the neural model did not

`keynet_spec.onnx` scored 85.2% exact on the corpus it trained on and 37.5% on a
held-out one — last of six paths (`validation/keynet/2026-08-12-held-out-evaluation.md`).
That is the signature of a fitted model.

This engine shows the opposite. It was tuned on 26 files and then measured higher
on 1337 unrelated ones. It is also plain signal processing — box-resampling to
16 kHz, area-normalised Hann frames, interpolated spectral peaks, local whitening,
harmonic pitch-class accumulation, whole-song gating, balanced major/minor
correlation — so there is no model file to install, to be missing, or to drift.

## What this does NOT establish

- **Tempo is not covered here.** The 26/26 within-2-BPM figure remains a
  development-set result on the folder used to tune it.
- **The classical held-out set is gone.** SWD (Zenodo 3968389) was a temporary
  fetch and is not preserved. Re-fetching it would test a genre neither corpus
  covers, and chroma profiles score 75-89 MIREX there, so it is the case most
  likely to find a regression.
- **Confidence is not calibrated.** The estimator reports a winner-minus-runner-up
  margin. That is the same KIND of quantity as the chroma margin, which is why the
  existing publish gate carries over, but it is not the same margin and the
  threshold has been inherited rather than re-derived.

## A tool defect fixed alongside

`SattariKeyDatasetEval` ended its report with

    BEST MIREX: HPCP chroma + Sha'ath (audio)  (54.55 / 100 over 1337 tracks)

while the estimator on the line above scored 65.12. The winner loop only ranked
(chroma, profile) pairs and never considered the detector, so anyone reading the
summary got the wrong answer. The estimator is now included in the ranking.
