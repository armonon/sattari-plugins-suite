---
type: review-and-fix
product: auto-pitch
branch: harmony-engine-and-portal-compass
base_commit: 4b44d1f
date: 2026-08-27
truth_label: internal validation only; nothing here is a shipping claim
status: 16 confirmed defects fixed; 1 deliberately not changed; golden re-blessed with cause identified
---

# Auto Pitch — adversarial review pass, and what it changed

A five-dimension review (real-time safety, engine/latency, threading/state, DSP
correctness, test honesty) with every significant finding independently
re-verified by a separate agent instructed to REFUTE it. Only findings that
survived refutation are recorded here. 11 low-severity items were also raised;
the ones fixed are listed at the end, and the one deliberately left alone is
argued rather than quietly dropped.

---

## 1. The headline: the default gate was RED, and CI could not see it

`AutoPitchRender` with no arguments — the golden fingerprint contract, and the
only gate in the repository that can observe a change in the printed sound — was
FAILING:

```
FAIL: render drifted from golden (residual 0.173507 > 0.0001)
  peak=0.2570 rms=0.0984 centroidHz=409.0     (golden: peak 0.430533 rms 0.085805 centroidHz 430.75)
```

Peak down 4.5 dB, RMS *up* 1.2 dB, centroid down 21 Hz: crest factor 5.0 -> 2.6.
Not float noise. `scripts/verify_all.sh` exited 1 with VERIFY FAILED, while
`ctest` and the whole CI matrix reported green — CI runs only `--smoke` and
`--hitracktest`, and neither can see the printed sound.

Meanwhile `validation/PRODUCT_RC_REPORT.md` (the newest RC record, dated
2026-08-25) asserted **"Automated gates: ALL PASS"** and stated that the only
remaining condition on shipping was the manual host matrix.

### Cause — an intended fix arriving by merge, not a regression

The golden blob was blessed at `4512076` (2026-08-25). `5438e30` "Fix Auto Pitch
distortion and real-vocal correction" (2026-08-06) removed this from
`applyModeCharacter`, on the Modern Rap style the golden render uses
(`style = 2.0f`):

```cpp
if (style == "Modern Rap") {
    float previous = 0.0f;                       // <-- reset EVERY BLOCK
    for (int i = 0; i < numSamples; ++i) {
        const auto edge = sample - previous;
        previous = sample;
        buffer.setSample (channel, i,
            juce::jlimit (-0.98f, 0.98f, sample * 1.02f + edge * 0.08f));
    }
}
```

`previous` resets to zero at every block boundary, so the first sample of every
block was multiplied by ~1.10 — a periodic discontinuity, and an inflated peak,
injected at the block rate. Removing it is why the peak fell and the centroid
darkened.

`git merge-base --is-ancestor 5438e30 4512076` -> **false**;
`git merge-base --is-ancestor 5438e30 HEAD` -> **true**. The golden was blessed
on a branch that had not yet absorbed the distortion fix, the fix arrived later
by merge, and nobody re-blessed. **The golden encoded the pre-fix, buzzier
sound.** The current output is the corrected one.

Re-blessed deliberately, after establishing the above and after confirming every
fix below is output-neutral on this render path. The gate now passes at residual
**4.9e-7**.

### So it cannot recur silently

CI's refusal to run the strict gate is legitimate and documented — Rubber Band
output is not bit-portable across platforms, so a 1e-4 fingerprint cannot be a
CI gate. But that left nothing at all watching the sound. `--smoke`, which CI
*does* run, now carries a **portable peak/RMS envelope check** against the
golden at 25% tolerance: platform float variation on a deterministic synthetic
input is far below 1%, while the drop that hid here was 40% of peak.

---

## 2. Formant Preserve silently stopped preserving  (HIGH, default build)

`PluginProcessor.cpp`, `processWithRubberBand`.

Rubber Band's formant scale is a **pre-shift envelope warp that the pitch
resampler then multiplies** — not an absolute target. The vendored source is
decisive (`third_party/rubberband/src/finer/R3Stretcher.cpp:1499-1501`):

```cpp
process_t formantScale = m_formantScale;
if (formantScale == 0.0) formantScale = 1.0 / m_pitchScale;   // <-- this is what "preserve" IS
```

The substitution of `1/pitchScale` is precisely what makes `0.0` mean preserve,
so the net formant multiplier is always `formantScale * pitchScale`. The plugin
passed `combinedFormant` raw, rendering `combinedFormant * pitchScale`.

At neutral the code correctly sends `0.0`, so the bug **armed itself the moment
the user moved Throat or Formant Shift off zero** — and then the formant envelope
swung note-by-note with each note's correction ratio:

| requested | note 80 ct sharp | note 40 ct flat |
|---|---|---|
| +7 st | +6.2 st | +7.4 st |

That is the chipmunk modulation Formant Preserve exists to remove. Worst case,
MIDI Target raises the correction limit to 1200 cents, so a requested +7 st can
render as +19 st.

Fixed: divide by the ratio actually in use. This also makes the RubberBand path
agree with the signalsmith path, which already got this right via
`compensatePitch=true` — the two engines previously produced different formant
positions from identical knob settings.

---

## 3. Classic lead compensation was buffer-dependent  (live performance)

`targetPitchScale` is a pure function of `lastPitchFrame` in Classic mode, and
that frame only refreshes once per `pitchHopSize` = 256 samples. The velocity
term divided the cents delta by the **host block time**, so it was inflated by
`hop/blockSize` at any buffer below the hop: at 64 samples three blocks in four
saw delta = 0 and contributed no lead at all, while the fourth saturated the
+/-75 ct clamp. The compensation degenerated into a clipped step once per hop.

512 samples contains exactly two 256-sample hops — so **the one buffer size the
entire suite tested was the one size at which the bug cancelled.** The
`--diag` mode additionally shadowed the block size with its own local `512`.

Fixed: measure velocity across the interval in which the frame actually moved,
and hold the resulting lead between frames. Measured, A/B, `--features`:

| Classic lead formula | block 512 | block 64 |
|---|---:|---:|
| no lead term at all | 15.8 ct | 12.3 ct |
| OLD (÷ host block time) | 5.1 ct | 3.9 ct |
| NEW (÷ actual frame interval) | **5.1 ct** | **2.9 ct** |

Identical at 512, as predicted. At 64 the fix delivers a correct continuous lead
instead of a clamp-saturated spike. The residual 512-vs-64 spread (2.2 ct) is
*smaller* than the no-lead baseline spread (3.5 ct), i.e. the lead term no longer
contributes buffer dependence; what remains is the inherent analysis-rate
difference.

**Open, and stated rather than buried:** the -20 ms offset was calibrated at 512
only. The mechanism is now correct at every buffer size, but whether -20 ms is
still the best value at 64 has not been swept.

`SATTARI_AUTOPITCH_RENDER_BLOCK` was added to the harness so any mode can be
re-run at a live-performance buffer. Its absence is why this survived.

---

## 4. Polyphonic and Live Mode ignored the concert reference

`polyVocoderFrame` hardcoded A=440 in three places, and
`SattariLivePitchEngine.h` hardcoded it in the planner, while the monophonic path
resolved MIDI against `currentReferenceHz()` (which honours Reference Detune and
Follow Song Reference). The snap grid is anchored at the reference, so in an
A=432 session the poly engine pulled the track 31.8 cents sharp of the mono
engine and the readout — and toggling Polyphonic shifted absolute tuning
mid-session. Both now take the plugin's reference.

---

## 5. Transpose-to-Key swooped on every loop and locate

`resetPitchState()` set `smoothedXposeScale = 1.0`, and `reset()` is the host's
transport-relocate / loop-wrap / bypass-off hook. The transpose branch then glided
from unity to the selected ratio over 60 ms, so every loop wrap produced an
audible swoop toward a setting the user had not changed. Unlike
`smoothedPitchScale` — where re-converging from unity is correct, because
correction should re-earn its target — the transpose ratio is a static user
setting with nothing to re-approach. Now seeded at the selected transposition.

---

## 6. Audio-thread malloc after Undo

`undoPitchEdit` / `redoPitchEdit` move-assign an undo snapshot into `pitchEdits`.
Snapshots are copies, so their capacity is their element count: the move discarded
the 4096-element buffer `prepareToPlay` reserved, and the next captured note
would `push_back` past capacity — malloc, copy, free, inside `processBlock`.
`setStateInformation` already re-reserved after its own move, which is what
establishes the invariant. Both now do too.

---

## 7. Key Map timeline was lost on every project reload

`getStateInformation` persisted the segments and the file NAME but neither
`keyMapWave` nor `keyMapDurationSec`. `KeyMapTimeline::hasData()` requires
`durationSec > 0`, so a restored session drew "Scan a track to map its key
changes" over a map that demonstrably existed, with a dead playhead — and
nothing could regenerate it, because only a bare filename was stored, never a
path. Both now persist (1024 peak bins, ~5.5 KB base64).

---

## 8. AutoKey verdict could be published torn

`detectedAutoRoot` / `detectedAutoScale` / `detectedAutoKeyConf` are one verdict
but were three independent stores, written from the message thread
(`timerCallback`, on a dropped-file result) and the audio thread
(`correlateAutoKeyHistogram`). Worse, inside the arbitration function mode and
confidence were stored *inside* the branches while the root landed at the end, so
every call left a window in which the triple described no detector's actual
answer — and that pair feeds the cross-plugin bus.

Now staged into locals and published through one seqlock-guarded helper, read
back as a coherent snapshot at the single point where the frame captures it.

---

## 9. PSOLA engine-swap race

The `psolaSwapReady` flag is also the ownership token for `pendingPsola`.
Clearing it with `exchange()` *before* the swap released ownership too early:
`handleAsyncUpdate` could observe (flag clear, `psolaLiveLowLat` not yet updated)
and call `preparePsolaInto` on `pendingPsola` while the audio thread was still
swapping it. Now cleared only after the swap and the publication complete.

---

## 10. Four defects on the signalsmith path

Not in the shipping build (the submodule is deliberately uninitialised) but all
real, and all fixed so the path is not a trap when it is enabled:

- **Reported latency 0 while the wet path ran ~4096 samples late.** `prepareToPlay`
  computed the correct figure from `ssStretch`, then ~100 lines later
  unconditionally overwrote it with `activeEngineLatencySamples()`, which had no
  signalsmith case and returned a RubberBand calculation that is 0 in a
  signalsmith build. The dry reference inherited the same 0, so toggling
  correction jumped the vocal ~93 ms. Now there is one authority that knows which
  engine actually runs.
- **Transpose-to-Key was a silent no-op.** `prepareToPlay`'s engine init was an
  exclusive `#if/#elif` chain, but the xpose branch dispatches
  `processWithRubberBand` under its own independent `#if HAS_RUBBERBAND` macro —
  so the stretchers were never constructed and the empty-vector guard returned
  dry. RubberBand now always initialises when compiled in.
- **Bypass kept the wrong engine warm**, priming RubberBand while signalsmith
  held a stale pre-bypass tail that replayed on re-engage.
- **`makeCopyOf` detached the host buffer** on any block shorter than
  `preparedBlockSize`, since it resizes the destination to the source's size.
  Replaced with a bounded `copyFrom`; oversized blocks now chunk rather than
  allocate on the audio thread.

---

## Low-severity items also fixed

- A junk `SATTARI_AUTOPITCH_CLASSIC_CONTROL_MS` silently **disabled** the lead
  compensation (`getDoubleValue()` returns 0.0 for unparseable input) instead of
  falling back to the default. A typo looked like a working build.
- Big Harmony's scratch buffer claimed "no realloc" unconditionally; true only
  while the host honoured its declared maximum. Now grows only when necessary.
- The engine readout said "Studio HQ" (Rubber Band) even in a signalsmith build.
- `parameterChanged` mirrored `scale` -> `scaleMode` with
  `setValueNotifyingHost`, which is neither legal nor RT-safe from the audio
  thread. Now inline on the message thread, deferred only when off it. **A first
  attempt that deferred unconditionally broke the offline render** (no message
  loop, so the mirror never ran and the render silently corrected to the wrong
  scale); caught by the golden gate, which is exactly what it is for.
- The elastique stub published a latency for a path that is never dispatched.

## Deliberately NOT changed

The review flagged the intent gate's stability factor as under-correcting the
first ~36 ms of every note. It is left alone. The code already carries the
threshold (`(transitionBoost - 0.55) / 0.45`) and the bounded 30% ease
specifically to fix the naive version, with a comment recording that scaling by
`transitionBoost` directly "under-corrected the attack of every single note and
systematically weakened fast passages — the opposite of the intent." It was also
the one finding never adversarially verified. Changing tuned DSP character on an
unverified low-severity hunch, immediately before real-vocal testing, is the
wrong trade.

---

## Gate state after the pass

```
core contract           ok            (engine=rubberband)
golden fingerprint      ok            residual 4.9e-7   (re-blessed, cause above)
--smoke                 ok            + NEW envelope check vs golden (peak/rms, 25%)
--nulltest              8/8 PASS      -inf dBFS, zero offset
--wetalign              reported 2885 (60.10 ms), worst wet-vs-dry 69 smp (1.44 ms)
--livetest              PASS          1037 smp / 21.60 ms, +45 ct -> +0.1 ct, bus key reaches planner
--features              ok
```

## What this pass does NOT establish

- No listening evidence for the formant fix. The math is proven against Rubber
  Band's own source and the numbers follow from it, but nobody has heard it yet.
- No DAW host matrix. Every figure here is offline.
- The -20 ms Classic offset is still a 512-sample calibration (see section 3).
- The signalsmith fixes are reasoned and compile-verified, but that path has
  never been measured end-to-end; enabling it still requires the full
  re-measurement the `processBlock` comment demands.
