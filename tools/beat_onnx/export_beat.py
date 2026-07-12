#!/usr/bin/env python3
"""Export a pretrained neural beat/downbeat tracker to ONNX for StemDeck.

Mirrors tools/demucs_onnx: run this once in your training env (the same one you
used for Demucs), commit the resulting .onnx, and install it alongside the
separation model. StemDeck's TrackAnalyzer loads it when present and uses its
beat/downbeat activations as the PRIMARY beat source, falling back to the
built-in dynamic-programming tracker when it is absent.

Two supported source models (pick with --backend):

  beat_this  — Foscarini/Böck 2024, transformer, current SOTA F-measure.
               pip install https://github.com/CPJKU/beat_this  (see their README)
  beatnet    — Heydari 2021, CRNN.  pip install BeatNet

I/O CONTRACT the C++ side expects (keep this stable):
  input  "mel"   float32 [1, frames, N_MELS]   log-mel spectrogram, see params
  output "beat"  float32 [1, frames]            per-frame beat activation (0..1)
  output "down"  float32 [1, frames]            per-frame downbeat activation (0..1)

Frontend params (MUST match the C++ mel extractor in NeuralBeatEngine):
  sample_rate = 22050, n_fft = 1024, hop = 441 (~50 fps), n_mels = 128,
  fmin = 30, fmax = 11025, log1p magnitude mel.
"""
import argparse
import numpy as np

SAMPLE_RATE = 22050
N_FFT = 1024
HOP = 441            # ~50 frames / sec
N_MELS = 128
FMIN, FMAX = 30.0, 11025.0


def build_beat_this(device):
    """Return an nn.Module mapping mel[1,T,128] -> (beat[1,T], down[1,T])."""
    import torch
    import torch.nn as nn
    from beat_this.model.beat_tracker import BeatThis          # API per beat_this README
    from beat_this.inference import load_checkpoint

    core = BeatThis()
    state = load_checkpoint("final0")                          # bundled pretrained checkpoint
    core.load_state_dict(state, strict=False)
    core.eval().to(device)

    class Wrapper(nn.Module):
        def __init__(self, m):
            super().__init__()
            self.m = m

        def forward(self, mel):                                # mel: [1, T, 128]
            out = self.m(mel)                                  # dict or tuple depending on version
            if isinstance(out, dict):
                beat, down = out["beat"], out["downbeat"]
            else:
                beat, down = out[0], out[1]
            return torch.sigmoid(beat), torch.sigmoid(down)

    return Wrapper(core).to(device)


def build_beatnet(device):
    import torch
    import torch.nn as nn
    from BeatNet.model import BDA                              # CRNN activations model

    core = BDA(272, 150, 2, device)                            # per BeatNet defaults
    core.eval().to(device)

    class Wrapper(nn.Module):
        def __init__(self, m):
            super().__init__()
            self.m = m

        def forward(self, mel):
            act = self.m(mel)                                  # [1, T, 3] (beat, downbeat, non)
            return act[..., 0], act[..., 1]

    return Wrapper(core).to(device)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--backend", choices=["beat_this", "beatnet"], default="beat_this")
    ap.add_argument("--out", default="beat-model.onnx")
    ap.add_argument("--frames", type=int, default=1500, help="dummy length for tracing (~30 s)")
    args = ap.parse_args()

    import torch
    device = "cpu"
    model = build_beat_this(device) if args.backend == "beat_this" else build_beatnet(device)

    dummy = torch.zeros(1, args.frames, N_MELS, dtype=torch.float32)
    torch.onnx.export(
        model, dummy, args.out,
        input_names=["mel"], output_names=["beat", "down"],
        dynamic_axes={"mel": {1: "frames"}, "beat": {1: "frames"}, "down": {1: "frames"}},
        opset_version=17, do_constant_folding=True,
    )
    print(f"wrote {args.out}  (backend={args.backend}, n_mels={N_MELS}, ~50 fps)")
    print("Frontend params the C++ side must match:")
    print(f"  sr={SAMPLE_RATE} n_fft={N_FFT} hop={HOP} n_mels={N_MELS} fmin={FMIN} fmax={FMAX} log1p-mel")


if __name__ == "__main__":
    main()
