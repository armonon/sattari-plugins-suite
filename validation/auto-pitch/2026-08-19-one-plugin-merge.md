---
type: feature-validation
product: auto-pitch
date: 2026-08-19
lifecycle: alpha (the merged line; RC3 print DSP itself is unchanged)
truth_label: internal validation only; nothing here is a shipping claim
status: MERGED, ADVERSARIALLY REVIEWED, MACHINE-VERIFIED — no listening evidence
---

# Auto Pitch absorbs Sattari Key: one plugin detects and corrects

Three commits (`bcd4074`, `91c3cfb`, and the review-fix commit following):
detection, UI, and the fixes from an adversarial review.

## What one plugin now does

- **Live AutoKey runs the best measured detector.** The `SongKeyEstimator`
  (57.6% exact / 65.12 MIREX on 1337 labelled tracks) drains the same ring the
  spectrogram model reads — a private cursor over the monotonic `akSpecFilled`
  counter turns the rolling ring into a continuous stream, so the audio thread
  gained zero work. Once warm (~4 s) and above the 0.10 margin it outranks the
  spectrogram model, whose 72.1 MIREX claim failed held-out evaluation.
- **Drop a song, get its key and tempo.** The editor is a drop target; the file
  is decoded into a finalised estimator plus a spectral-flux onset envelope for
  the `TempoEstimator`. The verdict outranks every live detector, reaches the
  bus even with the transport stopped, and obeys the No Result policy: below
  the 0.10 margin it is DISPLAYED ("low confidence — shown, not applied") and
  drives nothing.
- **Low latency is a visible mode.** The LIVE pill in the header engages
  `SattariLivePitchEngine` (1,037 samples / 21.60 ms vs the print path's
  60.10 ms), with the ACTIVE engine's reported delay beside it, read live from
  `getLatencySamples()` so the label cannot disagree with the host.
- **One page.** BASIC/PRO/EDIT tabs are gone: header, everyday controls, the
  always-visible graph, and the old PRO tab as a collapsible ADVANCED drawer.
  Verified by rendered screenshots at host-reachable sizes, drawer open and
  closed.

## The adversarial review, and why it earned its cost

A four-lens review (RT-safety, correctness, UI, contract coverage) with
refutation-first verification confirmed **10 real findings**, none caught by the
green test suite:

1. **Use-after-free (blocker).** `prepareToPlay` restarted the worker BEFORE
   reallocating the ring and estimator it reads. First prepare safe; every
   sample-rate/buffer change after that raced a freed atomics array. The
   worker now restarts last, unconditionally.
2. **The acquire that paired with nothing.** The drain acquire-loaded
   `akSpecFilled`, but the producer incremented it relaxed — no happens-before
   edge to the sample stores on ARM. One word: `fetch_add(release)`.
3. **The plain-DSP detector was gated on an ONNX model.** The worker only
   started `if (crepeModelPresent || ...)` — a build without the model (a
   documented recurring failure) would silently lose the headline detector.
4. **Dropped-file verdicts stalled until someone sang.** `correlate...` only
   runs on voiced/sidechain audio; with the transport stopped the verdict sat
   in atomics while the bus stayed silent. The 1 Hz heartbeat now applies it.
5. **Stale key survived project restore** via the derived latch even though the
   file verdict itself was cleared. Confidence zeroed + estimator rescan on
   `setStateInformation`.
6. **The drop banner painted where nobody could see it** — `paint()` runs under
   the opaque graph panel. Moved to `paintOverChildren()`.
7. **MIDI Learn had zero width at the editor's own minimum size** (fixed-width
   arithmetic consumed exactly the column). Proportional now.
8. **The CI screenshot rendered below the editor's minimum height**, clipping
   the drawer it exists to prove. Renders at 960x760 / 1120x820 now.
9. Lap-skip headroom (drain no longer starts on the live write position) and
10. the non-modal branch now writes its own margin instead of inheriting a
   frozen one from a different detector.

Also pre-empted before the review returned: `songKeyRescan` was consumed but
never set (wired into `resetPitchState`).

## Verification

| check | result |
|---|---|
| `--nulltest` | 8/8 at `-inf`, offset +0, both engines |
| `--livetest` | PASS (bus key reaches the live planner) |
| `--songfile` | both directions on real GiantSteps tracks: confident verdict wins; sub-gate verdict correctly not applied |
| `--wetalign` | unchanged, −93 samples |
| `--smoke`, core contract | pass |
| CPU p95 @48k/512 | 2.4–2.5 ms vs 10.67 ms budget |
| UI renders | default + min + drawer, visually inspected |

## What this is NOT

- **No listening evidence** — this is a NEW candidate line; the sealed RC3
  package no longer matches the shipping detector chain and a fresh package
  must be generated before Gate 3.
- **No host testing**, and CI is still billing-blocked: every number above is
  one machine's evidence.
- Sattari Key the plugin still exists; retiring it from the catalog is a
  product decision, not made here. Auto Pitch simply no longer needs it.
- The `keyLock` (freeze session snapshot) semantics from Sattari Key were NOT
  ported yet — the file latch covers the dropped-song case; a live-detection
  lock is open work.
