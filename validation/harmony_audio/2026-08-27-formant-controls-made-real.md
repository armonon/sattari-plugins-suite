---
type: fix-record
product: harmony
branch: harmony-engine-and-portal-compass
date: 2026-08-27
truth_label: internal validation only; nothing here is a shipping claim
status: formant controls now functional; engine moved R2 -> R3; latency and CPU measured
---

# Harmony — the Formant controls were inert, and now are not

## The defect

All eight per-voice Formant parameters (`v1formant` … `v8formant`) were exposed
in the UI, visible to host automation, and shipped with **non-zero defaults**
(`{0, 0, -1, +1, -2, +2, -1.5, +1.5}` semitones, with v3 enabled in the default
3-voice choir) — and did **nothing at all**.

Cause, from the vendored source rather than inference
(`third_party/rubberband/src/RubberBandStretcher.cpp:129-132`):

```cpp
void setFormantScale(double scale) {
    //!!!
    if (m_r3) m_r3->setFormantScale(scale);      // R3 ONLY
}
```

`setFormantScale` forwards only to the R3 engine. Harmony requested no
`OptionEngineFiner`, so it ran RubberBand's default R2, and every call was
dropped on the floor. `setFormantOption` *does* reach R2
(`if (m_r2) m_r2->setFormantOption(options)`), so formants were still
*preserved* — the shift amount was simply ignored. That is what made it
invisible: the voices sounded reasonable, just never responded to the control.

Nothing in the suite could catch it. Every existing Harmony proof drives a
**sine**, which has no spectral envelope for a formant shift to move.

## The fix

1. `OptionEngineFiner` — R3 is required for the feature to exist at all, and is
   what Auto Pitch already settled on for vocal material.
2. **Pitch-compensated formant scale.** RubberBand's formant scale is a
   pre-shift envelope warp the pitch resampler then multiplies
   (`R3Stretcher.cpp:1499` substitutes `1.0 / m_pitchScale` when the scale is
   0.0 — that substitution *is* what makes 0.0 mean "preserve"), so the net
   multiplier is always `formantScale * pitchScale` and the request must be
   divided by the ratio in use.

   This matters far more in Harmony than in Auto Pitch, because a harmony
   voice's ratio is a musical **interval**, not a correction nudge. Had R3 been
   enabled without the compensation, every voice with a non-zero default would
   have had its entire interval added on top of the requested formant:

   | voice | requested | would have delivered (a 5th up) | error |
   |---|---:|---:|---:|
   | v3 | −1.0 st | +6.0 st | +7.0 st |
   | v4 | +1.0 st | +8.0 st | +7.0 st |
   | v6 | +2.0 st | +9.0 st | +7.0 st |

3. `OptionPitchHighConsistency` — every voice's ratio *glides* (`smoothedRatio`
   is a one-pole toward the target). Without it R3 emits a phase discontinuity
   on each ratio move, which on a gliding voice is a click per block rather than
   a one-off.

Also fixed, latent: `smoothedRatio`, `humanizeCents` and `humanizeTarget` were
declared with four initialisers for an eight-element array — left over from when
`kNumVoices` was 4. Voices 5-8 therefore started at a pitch ratio of **0.0**
rather than unity. `prepareToPlay`'s `fill()` masks it, which is the only reason
it has never been heard.

## Proof — `HarmonyRender --formanttest` (new)

A **difference** measurement, which is immune to the sine problem that hid this:
render twice changing only `v1formant`, over a band-limited sawtooth. An inert
control produces bit-identical output.

| build | waveform difference | harmony centroid, 0 st → +6 st |
|---|---:|---|
| R2 (the defect, re-tested deliberately) | **0.00000 (0.00% of peak)** | 793.5 Hz → 793.5 Hz (+0.0) |
| R3 + compensation (now) | 0.22050 (27.40% of peak) | 1274.1 Hz → 2097.8 Hz (**+823.7 Hz**) |

The R2 row is the important one: changing a shipped, automatable control by six
semitones produced **bit-identical audio**. The gate fails on it, which is the
only reason to trust the passing row.

## Cost — `HarmonyRender --bench` (new)

3.0 s of audio, 48 kHz, block 512. Realtime = audio seconds per wall second.

| build | latency | 1 voice | 3 voices (default) | 8 voices |
|---|---:|---:|---:|---:|
| R2 (before) | 1024 smp (21.3 ms) | 8.6× | 6.1× | 3.6× |
| R3 standard | 2048 smp (42.7 ms) | 7.4× | 4.6× | 2.3× |
| R3 + Low Latency | 1280 smp (26.7 ms) | 8.9× | 5.9× | 3.4× |

**Reported latency doubles in standard mode**, 21.3 ms → 42.7 ms. That is a real
host-visible change and the honest cost of the feature. Low Latency mode
recovers most of it — 26.7 ms, only +5.3 ms over the old R2 figure, at
essentially R2's CPU. The dry path tracks it automatically
(`dryDelay.setDelay(startDelay)`), so the mix stays aligned in every mode.

**`lowLatency` now defaults ON** (changed deliberately, on request, after the
measurement above). A harmoniser is played, so 26.7 ms is the better default
than 42.7 ms, and it costs essentially nothing in CPU. The trade is a shorter
analysis window (`OptionWindowShort` in place of `OptionPitchHighQuality`),
which costs some smoothness on long sustained notes — turn Low Latency off for
print/mixdown work if that matters. Sessions saved before this restore whatever
value they stored, so no existing project changes behaviour.

Net effect for a new instance: **21.3 ms (old R2) → 26.7 ms**, +5.3 ms, with the
Formant controls working for the first time.

Gates re-run with the new default: contract OK, 5th proof ok, smoke ok
(peak 0.556), `--formanttest` ok (49.20% difference, centroid +1012.4 Hz).

## Now covered in CI

Harmony was **absent from the CI matrix entirely** — neither `HarmonyCoreContract`
nor `HarmonyRender` had ever run there. Added via `config/products.json`
(the generator's source of truth, not the generated file), and the CI render step
now runs `--formanttest` for Harmony the way it runs `--hitracktest` for Auto Pitch.

## Gate state

```
HarmonyCoreContract        OK
HarmonyRender (5th proof)  ok
HarmonyRender --smoke      ok (peak 0.570)
HarmonyRender --formanttest ok   27.40% difference, centroid +823.7 Hz
```

## What this does NOT establish

- **Nobody has heard it.** The measurement proves the control moves the spectral
  envelope in the right direction by roughly the right amount; it does not prove
  the result sounds good, or that the shipped per-voice defaults are musically
  right now that they finally do something.
- R2 → R3 changes the sound of every harmony voice. Harmony's existing validation
  (`HARMONY_AUDIO_RC_REPORT.md`, 100% scale-tone accuracy, 0 crashes over a
  308-clip corpus sample) was measured on **R2** and has not been re-run.
- No DAW host testing. No CPU measurement on anything but this machine.
