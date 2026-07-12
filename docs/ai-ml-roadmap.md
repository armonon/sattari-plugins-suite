# StemDeck AI/ML Feature Roadmap

Technical plan for the features that require an ML model or external service, i.e.
the items that **cannot** be delivered with inline DSP. Each section states the
target, a concrete model/runtime option, the integration surface in this codebase,
and a realistic effort tier.

Integration is decoupled via `RoadmapAdapters.h` — the app talks to abstract
adapters; a concrete model/service is dropped in behind each one without touching
the audio engine or UI.

Effort tiers: **S** = days, **M** = weeks, **L** = months / research-grade.

---

## 1. Next-gen neural stem separation  — tier **M**

**Today:** `RealTimeStemSeparator` is a DSP heuristic (biquad crossovers +
mid/side centre extraction). `StemProvider` shells out to **demucs** (real ML)
for offline, high-quality stems.

**Target:** real-time, near-zero-artifact separation on the audio thread.

**Approach:**
- Model: a lightweight causal separation net (e.g. a distilled/quantised
  Demucs-v4 or a Conv-TasNet variant) exported to **ONNX**.
- Runtime: **ONNX Runtime** (C++) or **CoreML** on Apple Silicon for ANE
  acceleration. Run inference off the audio thread on a lookahead ring buffer
  (~100–250 ms latency), not per-block, then crossfade band outputs.
- Surface: implement `NeuralStemAdapter : StemRemovalAdapter`. `RealTimeStemSeparator`
  gains a `setAdapter()` hook; when a neural adapter reports `isAvailable()`, the
  processBlock RTSS path pulls its 4 stem buffers instead of the biquad split.
- Fallback: keep the heuristic split as the zero-dependency default.

**Risk:** real-time budget on the ANE; artifact/latency trade-off. Ship as an
optional "HD Separation" toggle, offline-first.

---

## 2. Audio → MIDI (polyphonic) & sound transformation — tier **L**

**Today:** monophonic melody extraction (YIN) is buildable inline (see the
`audio-to-midi` DSP feature, separate track). Polyphonic note/chord extraction and
"hum → instrument" / "beatbox → drums" transformation are model-grade.

**Approach:**
- Polyphonic transcription: **basic-pitch** (Spotify, TF-Lite) or **MT3**
  (magenta) exported to ONNX/TF-Lite. Input a stem, output a note list → write to a
  `MidiClip` via the existing `MidiClipPlayer`/`PianoRollView`.
- Timbre transfer (hum→instrument, beatbox→drums): **DDSP** (magenta) or a RAVE
  model. Realtime RAVE is feasible on ONNX/LibTorch; offline render is safer first.
- Surface: `AudioToMidiAdapter` (returns note events) and `TimbreTransferAdapter`
  (returns rendered audio). Both invoked off the audio thread with progress
  callbacks, mirroring `StemProvider`'s ThreadPoolJob pattern.

---

## 3. Intelligent spectral mixing & smart mastering — tier **S–M**

**Note:** the *mastering* half (analysis → auto EQ/dynamics/LUFS) is buildable with
classic DSP and is tracked as an inline feature. The *cross-deck spectral
un-masking* half benefits from a model but has a strong DSP baseline.

**Approach:**
- Baseline (no ML): multiband spectral analysis across active decks; detect
  masking (overlapping energy in shared bands) and apply complementary EQ dips.
  LUFS via an ITU-R BS.1770 meter; match to genre/reference curves.
- ML upgrade: a small regressor trained on reference masters to predict target EQ
  tilt / dynamics from a mel-spectrogram. ONNX, off-thread, suggests curves the
  user confirms.
- Surface: `SpectralBalanceAdapter` and `MasteringAdapter` returning parameter
  suggestions (not audio) so the user stays in control and it's undo-able.

---

## 4. Generative AI set building — tier **M** (service-backed)

**Target:** "build a heavy 120 BPM set in C minor" → ordered track suggestions
from local library + streaming.

**Approach:**
- Analysis already exists: `TrackAnalyzer` provides BPM/key per track. Index the
  local library (`MusicLibraryView`) into an embedding/metadata store.
- LLM planner: a hosted API (the suite already uses a Netlify AI gateway pattern —
  see the librarian project) takes the prompt + candidate metadata and returns an
  ordered, harmonically-coherent set (Camelot-adjacent keys, energy arc, BPM
  ramp).
- Surface: `SetBuilderAdapter` (prompt in, ranked track list out). Streaming
  sources plug in behind a `LibrarySource` interface.
- Privacy: only send track *metadata* (never audio) to the service; make it opt-in.

---

## 5. Library interop — OneLibrary / Rekordbox / Traktor / Serato — tier **M**

**Target:** read/write cue points, loops, beatgrids, metadata across ecosystems.

**Approach:** pure parsing/serialization, no ML.
- Rekordbox: `rekordbox.xml` + the newer PDB (`export.pdb`, Kaitai schemas exist).
- Traktor: `collection.nml` (XML). Serato: `GEOB` ID3 tags + crate files.
- OneLibrary: adopt the emerging shared schema once stabilised.
- Surface: `LibraryInteropAdapter` with `importCollection()/exportCollection()`
  mapping to StemDeck's hot cues / loops / beatgrid model.

**Risk:** formats are reverse-engineered and versioned; scope to read-first,
write-behind-a-flag.

---

## Sequencing recommendation

1. Inline DSP features first (per-stem FX, audio→MIDI mono, smart-master baseline,
   automation lanes) — no external deps, immediate value.
2. Neural separation (offline HD toggle) — highest "wow", contained via adapter.
3. Library interop (read-only) — unlocks real-world libraries.
4. Generative set building — service-backed, reuses existing analysis.
5. Polyphonic transcription + timbre transfer — most research-heavy, last.
