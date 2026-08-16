# StemDeck — Master Project Brief

**Owner:** Armon Nasiri / Sattari Audio
**Status:** pre-1.0. Neural stack partially built; capture/arrange layer not yet started.
**Stack:** C++ / JUCE 8, ONNX Runtime, SQLite. Targets macOS and Windows.
**Repo:** github.com/armonon/sattari-plugins-suite — work on branch `unified`, in `~/Projects/StemDeck`.

---

## 1. What this product is

StemDeck is **a DJ performance surface married to a DAW arrangement editor**, for producing **studio mixes**, not playing live gigs.

1. Perform a mix on decks, by hand.
2. Everything is captured as editable intent, not as a recording.
3. Refine it in an arrangement view like a DAW.
4. Export at higher quality than what was performed.

**One-liner:** *Perform your mix, then produce it.*

**Target user:** mix-series producers, radio-show DJs, podcast/label mix commissions — anyone selling or publishing mixes.

### What StemDeck is NOT (explicitly deprioritized — studio-first)
- Live gig features: multi-channel cue out, HID/CDJ integration, booth-readable UI, panic recovery
- Slip / censor / loop-roll — live reflexes; edit afterward instead
- Broadcast / Icecast / RTMP streaming out
- Streaming service integration (Beatport/Tidal/SoundCloud) — studio mixes come from owned files

MIDI controller support IS wanted (basic learn-mode map is enough). No HID.

---

## 2. Non-negotiable architecture: capture intent, not audio

**Everything depends on this and it cannot be retrofitted.** Do NOT record the master. Record a **timestamped event stream against the master clock**: deck loads (identity, grid, content hash), transport events, every parameter change, per-deck tempo/pitch state, insert-plugin params on every stem lane.

The arrangement view is a **rendering of this stream**. Audio is rendered only at export.

| Feature | Needs the event stream because |
|---|---|
| Transitions as editable objects | The transition is a region of events, retimeable as a unit |
| Retroactive mixing | Source audio + separation still exist; the cut was a decision, not a destruction |
| Tempo re-export | Every event re-times against a new master tempo |
| Non-realtime render quality | Export re-renders from intent at full quality |

**Design the session file schema and capture event format before building anything downstream.**

---

## 3. Headline features

**Tier 1 (the demo):** transitions as editable objects; retroactive mixing (drag a stem lane's end to run a vocal longer); export at a different tempo than performed; stem-colored waveforms with intro/build/drop markers.

**Tier 2:** non-realtime render quality; comping transitions; stem freeze; cross-deck stem routing; overdub passes.

**Tier 3:** transition scoring *with explanation*; automatic first-pass mix; practice scoring (hold — different product).

**90-second demo, no narration:** perform a rough transition → drag it later → extend the outgoing vocal past the cut → change master tempo 124→128, everything re-times → export → the render is cleaner than the performance.

---

## 4. Instant load (architectural, not an optimization)

Three full-track analyses can't be made fast — make them non-blocking.
- **Playable in one buffer** (~2 s decode). Waveform, grid, key, stems arrive later.
- **Persistent analysis cache — do this first.** SQLite keyed by content hash: grid, key, energy, structure, peaks. Most loads are re-loads → instant for least work.
- **Progressive disclosure, visibly** — coarse then refine; DSP grid then neural snap.
- **Background pre-analysis** (idle; prioritize visible→crate→rest; pause during transport).
- **Speculative separation** on hover/queue.
- **Memory-mapped audio**; decode-on-demand for compressed formats.

**Order:** cache → progressive UI → speculative separation.

---

## 5. UI/UX — simple but a true powerhouse (depth on demand)
- One primary surface at a time (Perform vs Arrange are different screens).
- Progressive disclosure; direct manipulation over parameter lists; show state not modes.
- Fluidity is frame timing: GPU waveforms, 60fps floor, 150–250ms interruptible transitions, no UI-thread blocking ever, optimistic UI.
- Visual language djay is beatable on: stem-colored waveforms, structure markers, transitions as visible objects, a real dark aesthetic, typography as hierarchy (BPM/key large, rest recedes).

**How layout is enforced.** `plugins/stemdeck/src/LayoutStack.h` is a constraint
solver: rows and columns declare `min` / `preferred` / `grow` / `priority`, and it
shrinks lowest-priority-first and sheds whole items rather than ever overlapping
them. It exists because hand-rolled `removeFromX(<literal>)` chains silently
produced overlapping header controls and starved the stem waveform to a sliver at
1280x800. Verify layout changes with `StemDeckEditorRender` **at 1280x800**, not
just at 1680x1050 — the narrow case is where it breaks.

**Surfaces.** PERFORM is channels + master band + pads. ARRANGER is the energy
arc, the edit toolbar and the capture timeline. The arrangement used to be a
footer on the perform screen — the product's central claim rendered at the
smallest size on screen, competing for vertical space with the thing it was
meant to support. It owns its own surface now, and the perform screen gave the
reclaimed height to the channels.

**Three tiers of visual weight.** A channel strip carries ~40 interactive
controls; at one prominence there is no entry point for the eye. See
`applyDeckControlWeights()`:
- **Hero** — play, cue, level, filter, crossfade assign. The five things reached
  for without looking.
- **Standard** — EQ, pitch, stem gains, sync numerics. Set deliberately.
- **Recessed** — tab strip, cue pads, per-stem FX grid, beat-jump. Navigation and
  configuration, not performance.

It is rendering weight only: every control keeps the same hit target. When you
add a control, put it in a tier — an untiered control defaults to full weight and
competes with the transport.

**An idle channel is a drop target, not a cockpit.** Unloaded track decks take a
narrow fixed column and render the drop CTA; they do not render a full inert
control surface. Width goes to the channels doing work.

**One noun: "channel."** A channel is a track deck, an audio input, or a MIDI
instrument. The add button, the add tile, the count readout and the arranger
lanes all say channel. Do not reintroduce "deck" as a UI noun — the mismatch
(`+ CH` / `CHANNEL` / `DECK A` / "four deck lanes" beside five lanes) reads as
instability even when a user cannot articulate why.

---

## 6. Engineering constraints
Timing quality matters most (heard repeatedly on good speakers). Phase-locked master clock (continuous resample, never snap). Audio-thread discipline (no alloc/lock/IO in callback). Beat-grid editing without reanalysis. Variable-tempo as a full per-beat tempo map. Deep session-wide undo (from the event model). Render determinism. Session-file integrity.

**Library:** SQLite + FTS; background analysis queue; import rekordbox XML / Serato crates / Traktor NML (nobody switches if they lose cues+crates); smart crates; fingerprint dup detection; cloud sync of metadata only.

**Export:** stereo master; mix stems; Ableton `.als` + generic package; cue sheet/tracklist; the session file itself.

---

## 7. Neural stack status

**The neural stack is now actually IN the product.** For most of this project's
history `SATTARI_USE_ONNX` was OFF in every build config and the release script's
default — so every item below describes code that had never been compiled, let
alone run. It is now vendored (`third_party/onnxruntime-osx-universal2-1.22.0`,
gitignored, fetched by `scripts/fetch_onnxruntime.sh`), on by default in
`release_stemdeck.sh`, and both models have been loaded and run for real:
`StemDeckNeuralBeatSmoke` and `StemDeckNeuralKeySmoke` (see §10) exercise the
real `beat-model.onnx` / `keynet.onnx` against generated audio and assert their
confidence values are finite, in range, and actually discriminate — a click
track scores 0.99 beat-confidence against 0.00 for silence; a C-major triad
scores 0.90 key-confidence and is correctly identified as C, against 0.14 for
white noise. **This is still not an accuracy claim** — generated audio proves
the code path executes and produces meaningful numbers, nothing about real
music — but it is the difference between "this code exists" and "this code has
ever run," which for the beat-confidence math genuinely was the gap: enabling
ONNX for the first time immediately surfaced a compile error in it (a
`juce::jmax<size_t>` call resolving against `SIMDRegister` template
instantiation, `NeuralBeatEngine.cpp`), meaning the confidence computation this
whole section describes had never actually been built anywhere before.

Items 1–3 below were open at an earlier review and are closed in the sense that
the mechanism is in place and gated by a test — **not** that it has been
measured on real music.

1. ~~**Load latency.**~~ **DONE.** Decks return playable on the DSP result;
   neural runs on `loadPool` and hot-swaps via `refineAnalysisAsync`. Measured by
   `StemDeckLoadLatency` — a 300 s track is playable in **32 ms** and refined at
   617 ms, against a 200 ms bound. *Synthetic signals only.*
2. ~~**Unconditional neural override.**~~ **DONE for both.** The key model is
   gated on `kNeuralKeyConfidenceFloor` (0.45); the beat model carries a real
   `confidence` (activation strength + grid regularity) and is gated on
   `kNeuralBeatConfidenceFloor` (0.35). Below either floor the DSP answer stands.
   `TrackAnalysis::Source` records which engine won per field, and the deck
   header's NEURAL/DSP chip now actually reads that record — it used to read
   `deckHasNeuralStems()` (true for stems from ANY source, including the demucs
   CLI, and true even in ONNX-disabled builds), so it could and did print NEURAL
   for output the neural engine never touched.
3. ~~**Downsample aliasing.**~~ **DONE, UNVERIFIED.** A 6th-order Butterworth
   pre-filter now runs before the 22.05 kHz decimation. `LagrangeInterpolator`
   interpolates but does not band-limit, so everything above 11.025 kHz was
   folding down into the band the model reads — cymbals and hats, i.e. exactly
   the transients driving the onset envelope. The synthetic beat tests are
   unchanged by the fix, which is expected: they contain no HF content to alias.
   **This one can only be judged on real material.**
4. ~~**Naive peak-picking** (fixed 0.4 threshold).~~ **DONE, SYNTHETICALLY
   VERIFIED.** `BeatGridDecoder` now decodes neural activations with a
   probabilistic tempo-state/Viterbi path. It preserves clock continuity through
   quiet intros and breakdowns, permits gradual tempo drift, and is covered by
   `StemDeckBeatGridDecoderTest`. Licensed real-music corpus certification remains
   open; the mechanism is no longer fixed-threshold peak picking.

**Teardown.** Destroying a processor while neural refinement was in flight used
to SIGSEGV inside `onnxruntime::RunSince`. Teardown now cancels cooperatively,
terminates in-flight `Session::Run` calls, and joins the pools while the engines
are still alive; `StemDeckReplayHash` probes it. `NeuralStemEngine::separate()`
already had a cancel hook (`shouldCancel` + `cancelPending()`/`SetTerminate()`,
wired end-to-end from `StemProvider::shutdown()`) — but until
`StemDeckStemCancelSmoke` actually ran it against the real model, nobody had
discovered that `cancellationRequested` was a STICKY flag with no reset outside
`load()`: one cancellation left the engine reporting "Inference failed" on
every future call, permanently, for the life of the object. Both neural engines
had this bug (`NeuralBeatEngine`'s was worse — no reset even on reload); both
are now fixed (`Impl::beginOperation()` resets the flag at the start of each
public entry point) and both pass a real cancel-then-reuse test against real
models.

**Verified clean (don't re-litigate):** concurrency (`loadPool{4}` + shared engines —
local buffers + thread-safe `Session::Run`); fallback safety (additive; no model →
DSP stands); chunking fix (commit `02fac82`); cancellation reuse (`NeuralBeatEngine`,
`NeuralStemEngine`, both against real models — see §10).

**Process problem:** this project has repeatedly shipped code "verified" on synthetic/short inputs that fails on real material — and, this review found, has also shipped code that was never even COMPILED (SATTARI_USE_ONNX=OFF hid the beat-confidence bug above from every build, forever, until this review turned ONNX on). **Build a real-corpus, real-length CI test gate before adding features**, and treat "have we ever built this with the feature flag ON" as its own separate question from "does it work" — the second question is meaningless until the first is checked. Accuracy is honestly *unmeasured in StemDeck* — the 45%→56% key figure is a shared-core eval (`core/tools/key_dataset_eval.cpp` on GiantSteps+GTZAN, not this app); beat_this's F-measure is the paper's.

---

## 8. Separation
"Demucs is still a stub" is stale — checked directly, on both paths StemDeck
actually has. The `demucs` CLI is real (installed via
`scripts/setup_stemdeck_demucs.sh`, invoked by `StemProvider::runSeparatorCommand`)
and produces genuinely distinct stems — confirmed by ear/eye in the running app,
four visibly different per-stem waveform envelopes on a loaded deck, not a
placeholder. The on-device path (`NeuralStemEngine` + ONNX) loads the real
`neural-model.onnx`, separates, and — as of this review — cancels and resumes
correctly (§7). Neither path has been graded for separation QUALITY on real
music; that is a real gap, just not the one "still a stub" describes.

**Two-tier, as designed:** fast causal (~20 ms, lower quality) for live; full
Demucs running ahead of playback + again at export. Pre-separate on load into a
cache; re-separate at full quality at export. The live tier exists and is wired
in: `RealTimeStemSeparator` (biquad crossovers + mid/side + transient gating, no
FFT, no lookahead, RT-safe, and exactly reconstructs the dry signal by
construction — `bass+vocals+drums+music == input`, sample-exact), driven via
`MainPluginProcessor::isDeckRtssActive`. Not graded for separation quality
against real music either, same caveat as the other two paths above.

---

## 9. The Sattari suite as StemDeck's instrument/effect library
StemDeck is the host; the 42-plugin suite is its library (solves the 42-SKU problem). "A DJ-to-studio environment that ships with 40 key-aware processors." Bundle $199–299.

**Already usable:** Neural Key + session bus (key becomes a *timeline*); Auto Pitch (key-locked stem transposition — highest-value cross-product feature); Morph formant; Harmony Pro; the commodity EQ/Comp/Gate/Heat/Space/Echo/Shimmer as per-stem inserts; Riser/Gate-Seq/Transient/Imager as transition tools.

**Needs work:** Chord Delay, Resonator, Spectral Freeze, Unison, Vocoder. **Big:** VoxSynth in Arrange.

**Must build:** time-stretch engine (blocks tempo re-export — on the critical path); LUFS/true-peak metering; mix-bus limiter with lookahead; crossfeed monitoring.

**Integration caution:** wire 3–4 inserts first (EQ, Comp, Auto Pitch, one effect), prove hosting + render determinism, then expand.

---

## 10. Build order

**Done** (each has a test target; all on synthetic material unless noted):

1. ~~Analysis cache (SQLite, content-hashed)~~ — `AnalysisCache.{h,cpp}` over real
   `sqlite3`; `StemDeckAnalysisCacheTest`.
2. ~~Capture event format + session schema~~ — the one-way door, and it held.
   `EventStream.h`, `docs/event-stream-schema.md`, `docs/session-schema.md`.
   `StemDeckReplayHash` proves replay is **sample-identical** to the live pass.
3. ~~Fix the two open review issues~~ — see §7. Load latency is measured and
   closed; key confidence gating is in, beat gating is blocked on the model
   wrapper rather than skipped.

**Next — and item 4 is now the thing actually holding everything up:**

4. **Real-corpus test gate in CI** — **BUILT, NOT YET FED.** `StemDeckCorpusGate`
   grades the *effective* answer (the confidence-gated DSP/neural combination the
   app actually uses), reports DSP and neural separately so you can see whether
   the model earns its override, flags BPM octave errors distinctly from being
   lost, and refuses corpora that grade only one dimension. Generated audio is
   **measured** and rejected, not taken on trust — `StemDeckCorpusSelfTest` runs
   with the contracts and guards that detector.

   What remains is the corpus itself. CI is wired to a secret archive URL and,
   until that is configured, says out loud that accuracy is uncertified rather
   than substituting tones. **Every accuracy claim in this document is still from
   synthetic signals** — the anti-alias fix, the beat tracker and the load-latency
   figures included. Feeding this gate is what converts the rest of the list from
   "implemented" to "works".
5. Basic arrangement view (ugly but correct) — partly there (`ArrangementView`,
   `ArrangementBuilder`, the arrange lanes in the editor); the data model is
   proven by the replay gate.
6. Demucs two-tier + pre-separation cache. Also: give `NeuralStemEngine::separate()`
   a cancel hook (§7) — its `onProgress` callback is the place.
7. Library DB + rekordbox/Serato/Traktor import.
8. MIDI learn mapping.
9. Time-stretch engine (blocks tempo re-export).
10. GPU waveform renderer + visual language.
11. Capture → arrange → export pipeline complete (the differentiator).
12. Suite integration as stem inserts (3–4 first).
13. Harmonic tools on the key model.

1–8 make it a real program. 9–13 are why anyone switches.

---

## 11. Standing instructions for Claude
- Prefer the most complete implementation, not a minimal version.
- Be direct, honest, decisive. Don't soften findings. If a request is wrong, say why.
- **Do not claim "verified" without testing on real, full-length material.** Burned three times.
- Flag one-way doors explicitly.
- Scope honestly. This is a very large amount of work for one person; when a request implies months, say so.
