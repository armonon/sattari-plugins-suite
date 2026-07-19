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

---

## 6. Engineering constraints
Timing quality matters most (heard repeatedly on good speakers). Phase-locked master clock (continuous resample, never snap). Audio-thread discipline (no alloc/lock/IO in callback). Beat-grid editing without reanalysis. Variable-tempo as a full per-beat tempo map. Deep session-wide undo (from the event model). Render determinism. Session-file integrity.

**Library:** SQLite + FTS; background analysis queue; import rekordbox XML / Serato crates / Traktor NML (nobody switches if they lose cues+crates); smart crates; fingerprint dup detection; cloud sync of metadata only.

**Export:** stereo master; mix stems; Ableton `.als` + generic package; cue sheet/tracklist; the session file itself.

---

## 7. Known open issues in the neural stack (from last review)

1. **Load latency.** `makeLoadedAudio` runs three full-track analyses in series before a deck is ready (DSP analyze + chunked beat sweep ~2.3 s/min + 12-inference `detectTTA`). 5-min track → 10+ s. **Fix:** return playable on the DSP result; run neural on a background queue and hot-swap the grid when it lands.
2. **Unconditional neural override** (`MainPluginProcessor.cpp:835/851`) — neural replaces DSP whenever merely valid; KeyNet confidence computed but unchecked. **Fix:** gate on a confidence floor and **surface which engine won in the UI**.
3. **Downsample aliasing.** `LagrangeInterpolator` 44.1→22.05 kHz, no anti-alias pre-filter.
4. **Naive peak-picking** (fixed 0.4 threshold). Port beat_this's DBN; will fail on ambient intros/breakdowns.

**Verified clean (don't re-litigate):** concurrency (`loadPool{4}` + shared engines — local buffers + thread-safe `Session::Run`); fallback safety (additive; no model → DSP stands); chunking fix (commit `02fac82`).

**Process problem:** this project has repeatedly shipped code "verified" on synthetic/short inputs that fails on real material. **Build a real-corpus, real-length CI test gate before adding features.** Accuracy is honestly *unmeasured in StemDeck* — the 45%→56% key figure is a shared-core eval; beat_this's F-measure is the paper's.

---

## 8. Separation
Demucs is still a stub and everything depends on it. **Two-tier:** fast causal (~20 ms, lower quality) for live; full Demucs running ahead of playback + again at export. Pre-separate on load into a cache; re-separate at full quality at export.

---

## 9. The Sattari suite as StemDeck's instrument/effect library
StemDeck is the host; the 42-plugin suite is its library (solves the 42-SKU problem). "A DJ-to-studio environment that ships with 40 key-aware processors." Bundle $199–299.

**Already usable:** Neural Key + session bus (key becomes a *timeline*); Auto Pitch (key-locked stem transposition — highest-value cross-product feature); Morph formant; Harmony Pro; the commodity EQ/Comp/Gate/Heat/Space/Echo/Shimmer as per-stem inserts; Riser/Gate-Seq/Transient/Imager as transition tools.

**Needs work:** Chord Delay, Resonator, Spectral Freeze, Unison, Vocoder. **Big:** VoxSynth in Arrange.

**Must build:** time-stretch engine (blocks tempo re-export — on the critical path); LUFS/true-peak metering; mix-bus limiter with lookahead; crossfeed monitoring.

**Integration caution:** wire 3–4 inserts first (EQ, Comp, Auto Pitch, one effect), prove hosting + render determinism, then expand.

---

## 10. Build order
1. **Analysis cache (SQLite, content-hashed)** — most loads instant for least work.
2. **Capture event format + session schema** — the one-way door.
3. **Fix the two open review issues** (load latency + confidence gating/surface which engine won).
4. **Real-corpus test gate in CI** — before features. Non-negotiable.
5. Basic arrangement view (ugly but correct) — prove the data model.
6. Demucs two-tier + pre-separation cache.
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
