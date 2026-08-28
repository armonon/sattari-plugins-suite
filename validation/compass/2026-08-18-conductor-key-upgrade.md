---
type: gate-finding
product: brain (Sattari Compass)
date: 2026-08-18
gate: 0
truth_label: internal validation only; nothing here is a shipping claim
status: GATE 0 DETECTOR QUESTION RESOLVED WITH MEASURED DATA
---

# Compass: the conductor now runs the best measured detector

## The defect, restated with current numbers

The Compass campaign blocked Gate 0 on one finding: *the conductor runs the
weakest detector in its own product family, and it outranks the better one.*

That did not improve when Sattari Key did — it got worse. Sattari Key adopted
`SongKeyEstimator` on 2026-08-16; Compass kept calling `sattari::dsp::detectKey()`.
Measured on 1337 labelled tracks:

| plugin | bus priority | detector | exact key |
|---|---:|---|---:|
| Compass | **100** | Aarden / HPCP | **39.2%** |
| Sattari Key | 80 | SongKeyEstimator | **57.6%** |

`publisherPriority()` gives `brain` 100 and `song-key` 80, and there is one bus
slot. So in any session with both loaded, **the conductor overwrote a 57.6%
answer with a 39.2% one**. The gap had roughly tripled from the ~5 points that
originally blocked the gate.

## What changed

Compass now detects with `SongKeyEstimator`, falling back to the HPCP/Aarden path
until the estimator has heard enough audio to have an opinion (~30 s). The HPCP
path is retained regardless: it still produces the detune estimate and the triad
evidence, and it is what answers in the opening seconds.

Three things this deliberately did **not** do:

- **No new dependency.** The estimator is plain DSP. There is no model to install
  and none to be missing, so the "may enhance, must not be load-bearing" rule is
  satisfied by construction rather than by a fallback path nobody exercises.
- **No change to the publish gate.** `kMinPublishConfidence` and the stability
  hysteresis are untouched, and both detectors report a winner-minus-runner-up
  margin, so the gate keeps its meaning. They are not the SAME margin though —
  computed over different features — so the threshold is inherited and should be
  recalibrated against measured data before anyone leans on its exact value.
- **No neural path.** The held-out evaluation stands; nothing here revisits it.

### The feed, and why it is not the existing ring

The estimator needs continuous audio. Compass's `ring` is one 4096-sample FFT
frame — about 85 ms — while the worker wakes on a ~700 ms cadence, so feeding it
from there would have shown the estimator roughly an eighth of the session, in
disconnected slices.

It therefore has its own feed: `processBlock` writes every mono sample to a
lock-free ring sized for four seconds, and the worker drains whatever arrived.
Single producer, single consumer, no locks, and no allocation on the audio thread
— the buffer is allocated in `prepareToPlay`. If the worker ever falls a full
buffer behind, the overrun is **counted** (`keyFeedDropped`) and the read cursor
skips forward, because a key built from torn audio is worse than one built from
less audio.

`requestRescan()` resets the estimator and the cursor as well. Without that,
"rescan" would have kept answering with the chroma of everything played before it.

## Verification

`BrainContract` now asserts the detector end to end: ~48 s of a C major triad fed
in real-time-sized chunks, with the worker left on its own cadence.

```
  songKey: valid=1 key=C major conf=0.2766
Sattari Compass contract ok
```

Audio is fed in paced chunks rather than flat out, and `analyseNow()` is not
force-called, for two reasons worth keeping: the worker-thread state has a single
owner and calling it from the test would race it, and the feed is sized for a live
stream, so pushing 48 s in at once laps the ring and is counted as dropped —
correct behaviour, but not what a DAW does.

## Gate 0 status

- [x] Decide the detector question — resolved with measured data, not a preference
- [x] Re-measure on labelled corpora — 57.6% exact / 65.12 MIREX over 1337 tracks
- [x] Contract-test the change
- [ ] Freeze a candidate revision
- [ ] Write the promotion record

Still open, and not addressed here: confidence calibration on measured data, the
chord/section/energy outputs which remain unvalidated, and the three-publishers
question about who may occupy the bus slot with a user-chosen key.

**This is an accuracy and correctness result, not a lifecycle promotion.** Compass
stays `alpha` until the remaining Gate 1 and Gate 2 items are done.
