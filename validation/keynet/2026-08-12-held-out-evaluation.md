---
type: held-out-evaluation
subject: keynet_spec.onnx + chroma profiles, via KeyAnalysisEngine
date: 2026-08-12
truth_label: internal validation only
status: THE +20 MIREX CLAIM IS NOT SUPPORTED
---

# Key detection on genuinely held-out data

Every previous number for this model was measured on data it trained on.
`scripts/train_keynet_cqt.py` trains on GiantSteps + GTZAN (true labels) plus
FMA (pseudo-labels), which is all three corpora in this repository.

The Schubert Winterreise Dataset is outside all of them: 48 tracks, two
complete performances of the 24-song cycle, expert global-key annotations,
CC BY 3.0, from Zenodo record 3968389.

## Result

| path | GiantSteps 500 (train) | GTZAN 837 (train) | **SWD 48 (HELD OUT)** |
|---|---:|---:|---:|
| chroma Aarden — what Compass runs | 47.34 / 39.8% | 46.30 / 38.8% | **88.12 / 85.4%** |
| chroma Bellman | 44.84 / 37.6% | 47.30 / 41.8% | **88.75 / 85.4%** |
| chroma Temperley | 48.86 / 39.0% | 49.81 / 43.2% | 86.88 / 83.3% |
| chroma Sha'ath | **56.56 / 45.2%** | **53.36 / 43.6%** | 80.00 / 75.0% |
| chroma Krumhansl | 51.90 / 38.8% | 52.22 / 41.6% | 75.00 / 66.7% |
| **NEURAL keynet_spec** | **87.44 / 85.2%** | **64.50 / 57.1%** | **55.00 / 37.5%** |

MIREX x100 / raw exact.

## Two conclusions, and one of them reverses an earlier recommendation

**1. The neural model does not survive held-out data.** 85.2% exact on the
corpus it trained on, 37.5% on one it did not — last of six paths, below every
chroma profile including the one it was supposed to replace. The +20.2 MIREX
improvement is not established; on the only uncontaminated evidence available it
is a large regression.

**2. Sha'ath would have been a REGRESSION, not a free win.** Two days of
measurement on GiantSteps and GTZAN said switch Aarden -> Sha'ath for +9.22 and
+7.06 MIREX. On held-out data that same switch costs **-8.12 MIREX and -10.4
points of exact key**. Aarden, the profile I proposed replacing, ties for best.

The original choice of Aarden was made on real audio for a stated reason — exact
key matters more than partial credit because a fifth error tunes to the wrong
scale — and it holds up where it counts.

## Confidence calibration does not hold out either

On training data the model's confidence was beautifully monotonic (GiantSteps
0.8-1.0 -> 99.0% exact). Held out it is not:

    0.2-0.4 ->  0.0%      0.4-0.6 -> 42.9%
    0.6-0.8 -> 20.0%      0.8-1.0 -> 66.7%

0.6-0.8 scores WORSE than 0.4-0.6. A threshold derived from the training-set
curve would be actively misleading in a session, which matters because Compass
is meant to gate on exactly this number.

## What this evidence cannot say

**48 tracks, one composer, one instrumentation.** Classical lieder is piano and
voice: clear tonal centres, no drums, no bass-heavy production. That is EASY for
a chroma profile, which is why every profile scores 66-85% here against 38-45%
on GTZAN. It is also maximally far from the model's training distribution.

**Two confounds cannot be separated with this data.** SWD differs from the
training corpora in genre AND in duration (1-6 minutes against GTZAN's 30 s
excerpts). `LogSpec` pools any input to 256 frames, so a long track is
compressed more aggressively. Whether the model fails on classical, on long
form, or on both is not established here.

So this does not prove the model is bad. It proves the +20 MIREX claim has no
support outside its training data, and that a decision made on contaminated
evidence would have been wrong in a measurable direction.

## What Compass should do with this

- Do NOT switch the profile on GiantSteps/GTZAN evidence.
- Do NOT wire the model into the conductor on the strength of 72.1.
- Held-out evaluation across genres is now a Gate 1 blocker, ahead of the
  profile question.
- The `KeyAnalysisEngine` remains the right structure regardless: it is what
  made this comparison possible in one tool, and it is what will let a future
  model be swapped in behind a config rather than a rewrite.
