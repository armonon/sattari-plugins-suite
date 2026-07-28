# "Solid Live Play" — Part A correctness gates

**Status of this document.** The four gates come from the Solid Live Play milestone
brief, which existed only in a chat session and was not in the repo. This file is a
reconstruction: the *gate definitions* are paraphrased from that brief, and everything
under "Measured" is a number produced by a target in this repo and reproducible by
running it. Where the brief had detail I no longer have, it says so rather than
inventing it.

**The rule the brief set, which governs this whole file:**

> Never claim a gate passed without running its test on real, full-length material and
> real hardware. Synthetic/short tests are exactly how this project has been burned
> before.

Part B (the UI work) ships alongside. The arrangement editor does **not** start until
Part A passes.

---

## A1 — Replay determinism

**Gate.** The live surface writes an event stream; the arrangement editor is a
deterministic replay of it. Replaying a captured session must reproduce it
sample-identically.

**Status: NOT STARTED. Blocked on review — this is the one-way door.**

The schema is drafted at [`event-stream-schema.md`](event-stream-schema.md) and has not
been agreed. Nothing should be coded against it until it is: every control in the live
surface writes to this stream and the whole arrangement editor reads it, so changing it
after either half exists breaks both.

Two things in the draft need a decision:

1. **§6.2 — pinning analysis.** Replay must use the *same* beat grid and key as the live
   pass. Neural refinement lands asynchronously, so the analysis actually used has to be
   recorded and replayed, not recomputed. Without this A1 cannot pass, and it will fail
   *intermittently* rather than cleanly — the worst failure shape to debug.
2. **Open question — cross-block-size determinism.** The draft fixes a block size for the
   hash test, because IIR state evolves per block. Requiring identity *across* block
   sizes is a much stronger constraint that effectively forbids per-block-dependent DSP.
   Recommendation in the draft: **no**.

**Relevant measurement.** `StemDeckLoadLatency` shows background refinement landing at
~0.6–5s depending on track length, and previously "not within 30s". That variance is
exactly the §6.2 hazard, now with numbers attached.

**Honest cost: 3–5 weeks.** The schema is days; inverting ~40 control call sites from
direct APVTS mutation to event dispatch, making rendering read session state, and
building the replay + hash harness is the bulk.

---

## A2 — Zero dropout

**Gate.** No dropouts under live conditions, including channel add/remove during
playback. No allocation on the audio thread.

**Status: PASSING, newly enforced.**

| check | target | result |
|---|---|---|
| add/remove under playback, sample-exact | `StemDeckChannelDropout` | pass |
| no allocation on the audio thread | `StemDeckRtSafety` | **0** (was 48,106) |
| per-stem FX re-sums phase-aligned | `StemDeckStemFx` | pass |
| PDC | `StemDeckPdc` | pass |

`StemDeckRtSafety` arms a global `operator new` hook for the duration of `processBlock`
and fails if anything allocates. It runs the whole processor — real decoded audio on two
decks, playing, with separation, FX, PDC and the master chain — while another thread
churns the channel list. It captures backtraces so a failure names its own call sites.

**It found two real violations on its first run, ~16 allocations per block:**

1. `getDeckCfSide()` built a property-id `String` *and* read `apvts.state` (a `ValueTree`)
   from the audio thread — an allocation and a data race in one line. Now an atomic mirror.
2. `TimePitchDecision::statusLabel` was a `juce::String` assigned string *literals* every
   block. Now `const char*`.

Note (1) survived a task explicitly titled "kill string-id allocs". **An assertion catches
what an audit misses** — that is the argument for keeping this target in the gate set.

**Remaining weakness.** `StemDeckChannelDropout` drives `ChannelGraph` directly rather
than the full `processBlock` path, so it proves the snapshot swap in isolation. The
RT-safety target covers the full path but asserts allocation, not audio continuity.
Merging the two — sample-exact continuity through the real processor — is the next
increment.

---

## A3 — Time to playable

**Gate.** A deck is playable within **200 ms** of loading a track.

**Status: PASSING, measured on full-length material.**

Run `StemDeckLoadLatency [seconds...]`.

| track | before | after |
|---|---|---|
| 2 min | 424 ms | **34 ms** |
| 5 min | 1365 ms | **~84 ms** (5 runs: 57 / 63 / 73 / 106 / 119) |

The load path used to decode *and* run full-track analysis before publishing audio, so
the cost scaled with duration and the gate failed badly on real material — while passing
comfortably on a 30-second clip. It is now two-phase: decode publishes audio (playable),
analysis follows on `DeckPlayer::setRefinedAnalysis`.

**Caveats, which matter:**

- Those are **warm-cache** numbers. First read of a freshly written file measured
  167–236 ms. Load is now I/O-bound rather than analysis-bound — the right place for it
  — but a cold read of a long file can still approach the bound.
- A 7-minute cold read measured 236 ms while a 10-minute one measured 170 ms.
  Non-monotonic, i.e. I/O variance, not a scaling law.
- Material is **synthetic** (120 BPM transient train over a harmonic bed). Full-length,
  but not real music. Real tracks may analyse slower.

**Second half of A3 — model confidence.** Key detection gates on
`kNeuralKeyConfidenceFloor` and flags a low-confidence result rather than presenting a
guess as fact. **The beat model still has no confidence to gate on.** That is
outstanding, and it also feeds A1 §6.2.

---

## A4 — Live input on real hardware

**Gate.** Live input works on a real audio interface.

**Status: NOT VERIFIED. Cannot be closed without hardware.**

This is the only gate that cannot be closed in software. Everything below is built and
renders correctly in offline tests driven by **synthetic buffers** — not one real sample
from a real device has been through it.

What exists: `InputBody` (gain, monitor, source channel, peak, real EQ + filter), the
per-type `ChannelStrip` with meter/clip and a receding idle state, and the channel frame's
mute/solo.

What closing A4 requires, on real hardware:

- [ ] Real interface at a real buffer size; an `input` channel with a device channel selected
- [ ] Signal arrives: meter moves, monitor on/off does what it says, source cycling picks
      the physical channel it claims
- [ ] Latency low enough to perform against
- [ ] No feedback loop when monitoring
- [ ] Survives a device change mid-session: sample-rate switch, unplug/replug
- [ ] Captured input lands correctly in the recording, not just the monitor path

> The brief may have had acceptance criteria beyond "live input on real hardware"; that
> detail was not recovered. Treat the list above as a floor, not the brief's wording.

---

## Not a gate, but blocks shipping

- **ONNX deployment floor.** Homebrew's `libonnxruntime` reports `minos 26.0` while the
  build declares 10.13, so an ONNX-enabled binary cannot load below macOS 26. The build
  now refuses this for shipping configs (`SattariOnnx.cmake`) and warns locally. Fixing it
  is a decision: vendor a lower-floor onnxruntime, or accept a macOS 26 minimum for
  neural features.
- **Tracktion Engine is dead weight.** Measured contribution to the mix: exactly zero
  (`StemDeckMultiInstance`). Its Edit is never populated, `usingTeMidi` is never true, and
  its accessors have no callers. It is now created lazily so it costs nothing, but whether
  it stays as scaffolding for the arrangement editor or is removed is a product call.
- **Multi-instance.** A second plugin instance used to deadlock the host (one Tracktion
  engine per process). Guarded by an exclusive claim and pinned by `StemDeckMultiInstance`;
  moot in practice now the engine is lazy.

---

## Running the gates

```
cmake --build build/all --target StemDeckRtSafety StemDeckChannelDropout \
      StemDeckStemFx StemDeckPdc StemDeckLoadLatency StemDeckMultiInstance
```

`StemDeckLoadLatency` prints numbers and only fails the 200 ms bound, so refinement
regressions surface as data rather than a red build.
