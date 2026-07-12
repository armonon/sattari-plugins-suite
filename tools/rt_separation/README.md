# Real-time neural stem separation — plan

## Where StemDeck stands today

Two separation paths already ship:

| Path | File | Latency | Quality | Use |
|------|------|---------|---------|-----|
| **Offline neural** | `NeuralStemEngine` + htdemucs ONNX | seconds (background) | **SOTA** (Demucs v4) | pre-separate a loaded track into 4 stem buffers |
| **Real-time DSP** | `RealTimeStemSeparator` | **zero** | heuristic, exact-reconstruction | live per-buffer stem knobs before separation completes |

This is a genuinely good split: htdemucs gives *higher separation quality than djay
Pro's real-time Neural Mix*, and the DSP path gives an instant, artifact-free
(bit-exact re-sum) fallback while it computes. The one thing djay does that StemDeck
does not is **real-time *neural*** separation — running a trained model per buffer so
the very first playback is already model-quality.

## The gap and why it's hard

htdemucs is ~far too heavy for per-buffer inference (it needs seconds per song, and
is non-causal — it looks at the whole track). Real-time neural separation needs:

1. A **causal / low-lookahead** model (bounded to a small ring buffer, e.g. ≤ 100 ms).
2. A **small** model (must run in a fraction of the buffer period on CPU/CoreML).
3. Streaming state (recurrent or overlap-add STFT with carried context).

## Recommended approach (in order of effort/quality)

### A. Stream htdemucs with look-ahead latency (fastest to ship, best quality)
Run the existing htdemucs ONNX on a **sliding window** on a background thread, one
`segment` (~7.8 s Demucs frame) ahead of the playhead, and read already-separated
stems from a jump-ahead buffer. Not truly zero-latency, but gives *model-quality*
stems within a second or two of pressing play, transparently upgrading the DSP path
as chunks complete. **No new model needed** — pure engineering on `NeuralStemEngine`.
This is the highest ROI and should be done first.

### B. Train a small causal separator (true real-time, matches djay)
Architecture options, cheapest first:
- **Band-split RNN (BSRNN)-lite** or **Conv-TasNet-lite** — causal, ~1–3 M params,
  runs comfortably real-time on CoreML. Trainable on MUSDB18-HQ (the standard 4-stem
  set) in the same env you trained/exported Demucs.
- **Hybrid**: distill htdemucs → the small causal net (teacher/student) so the RT
  model inherits htdemucs quality on your material.

Export it to ONNX with the **same I/O contract** as `NeuralStemEngine` already expects
(`[1, ch, samples] → [1, 4, ch, samples]`, order drums/bass/other/vocals), but process
in small hops with carried recurrent state. Because the contract matches, wiring is a
`NeuralStemEngine` streaming mode, not a new subsystem.

### C. Keep the DSP path as the always-on floor
`RealTimeStemSeparator`'s exact-reconstruction guarantee stays the zero-latency
fallback and the "riding the knobs never cancels" safety net, regardless of A/B.

## Verification harness (reuse)

`StemDeckNeuralSmoke` already runs a real file → 4 stems through the exact ORT path
and checks per-band energy. A streaming/causal model reuses it directly (add a
segment-latency assertion), so the RT path is verifiable the moment a model exists —
same as the Demucs pipeline.

## Bottom line

Do **A first** — it closes most of the perceived gap (model-quality stems shortly
after play) with zero new ML, just streaming logic on `NeuralStemEngine`. Pursue **B**
(the small causal net) only if true per-buffer neural separation is required; it is a
bounded MUSDB18 training job, the same shape as the Demucs export you already ran.
