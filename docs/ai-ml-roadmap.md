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

## 1. Next-gen neural stem separation — engine implemented / model shipping open

**Today:** `RealTimeStemSeparator` remains the zero-dependency DSP preview.
`NeuralStemEngine` implements background ONNX waveform separation with overlap-add,
streaming playhead-first preview, cancellation, cache publication, and CoreML/CPU
runtime support; `StemProvider` retains **demucs** as an external fallback. The
runtime/model are deliberately opt-in and no commercially cleared weights ship by
default.

**Target:** zero-setup, offline-first HD separation with progressive availability.
Hard realtime inference on the audio callback is not a release requirement.

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

## 2. Audio → MIDI and sound transformation — transcription implemented / timbre transfer open

**Today:** monophonic YIN melody extraction and drum-onset extraction feed editable
MIDI clips/Piano Roll. `NeuralTranscriber` integrates Spotify basic-pitch through a
background command adapter with YIN fallback. Timbre transfer remains unimplemented.

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

## 3. Intelligent spectral mixing & smart mastering — baseline implemented

**Today:** `SmartMaster` provides a bounded, undoable DSP mastering baseline with
spectral tilt, dynamics, loudness feedback, modes, and captured arrangement
automation. Cross-deck complementary spectral un-masking remains open.

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

## 4. Generative AI set building — deterministic local baseline / service planner open

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

## 5. Library interop — OneLibrary / Rekordbox / Traktor / Serato — open, tier **M**

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

1. **Done:** per-stem FX, mono/drum audio→MIDI, Smart Master, and automation lanes.
2. **Engine done; distribution open:** offline/progressive neural separation.
3. **Next:** read-only Rekordbox/Traktor/Serato library interop.
4. **Then:** opt-in service-backed set planning over local metadata.
5. **Last:** timbre transfer and research-heavy transformation models.
