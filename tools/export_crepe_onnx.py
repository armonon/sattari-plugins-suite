#!/usr/bin/env python3
"""
Export a CREPE pitch model to the ONNX format Sattari Auto Pitch expects.

Sattari's CrepePitchTracker (core/include/sattari_audio_core/SattariCrepePitch.h)
feeds a [1, 1024] float32 frame at 16 kHz and reads a [1, 360] salience vector,
which it decodes to a frequency. This script loads a CREPE Keras model and writes
an ONNX graph with exactly that input/output contract.

--------------------------------------------------------------------------------
Why this is a separate step (not bundled): the trained weights are TensorFlow
artifacts and the conversion needs TensorFlow, which does not run on every dev
machine (and, notably, not on Python 3.13/3.14 yet). So we ship the recipe, not
the binary. Run this ONCE in a clean Python 3.10-3.12 environment; the plugin
picks the model up automatically.

Usage:
    python3 -m venv .crepe-venv && source .crepe-venv/bin/activate   # py 3.10-3.12
    pip install "crepe==0.0.15" "tf2onnx>=1.16" "tensorflow>=2.12,<2.17" onnx
    python tools/export_crepe_onnx.py --capacity tiny

    # -> writes ~/Library/Sattari Audio/models/crepe.onnx  (macOS)
    #    (override with --out). Then build the plugin with -DSATTARI_USE_ONNX=ON
    #    and enable "Neural Pitch" in the Pro tab.

Capacities: tiny (fastest, ~487 KB) .. full (most accurate, ~86 MB). tiny/small
are the realtime-sensible choices.
"""
import argparse
import os
import sys
from pathlib import Path


def default_out() -> Path:
    # Mirror juce::File::userApplicationDataDirectory: ~/Library on macOS, and a
    # sensible per-platform equivalent otherwise. Auto Pitch looks here.
    if sys.platform == "darwin":
        base = Path.home() / "Library"
    elif os.name == "nt":
        base = Path(os.environ.get("APPDATA", Path.home()))
    else:
        base = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    return base / "Sattari Audio" / "models" / "crepe.onnx"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--capacity", default="tiny",
                    choices=["tiny", "small", "medium", "large", "full"],
                    help="CREPE model size (default: tiny)")
    ap.add_argument("--out", type=Path, default=None,
                    help="output .onnx path (default: platform models dir)")
    ap.add_argument("--opset", type=int, default=17, help="ONNX opset (default 17)")
    args = ap.parse_args()

    out = args.out or default_out()
    out.parent.mkdir(parents=True, exist_ok=True)

    try:
        import numpy as np
        import tensorflow as tf
        import tf2onnx
        import crepe
        from crepe.core import build_and_load_model
    except ImportError as e:
        print(f"Missing dependency: {e}\n"
              "Install into a Python 3.10-3.12 venv:\n"
              '  pip install "crepe==0.0.15" "tf2onnx>=1.16" '
              '"tensorflow>=2.12,<2.17" onnx', file=sys.stderr)
        return 2

    print(f"Loading CREPE '{args.capacity}' model ...")
    model = build_and_load_model(args.capacity)

    # CREPE's Keras model takes [batch, 1024] frames and outputs [batch, 360].
    # Freeze that exact signature so the ONNX graph matches SattariCrepePitch.h.
    spec = (tf.TensorSpec((1, 1024), tf.float32, name="frames"),)
    print(f"Converting to ONNX (opset {args.opset}) ...")
    model_proto, _ = tf2onnx.convert.from_keras(
        model, input_signature=spec, opset=args.opset, output_path=str(out))

    in_names = [i.name for i in model_proto.graph.input]
    out_names = [o.name for o in model_proto.graph.output]
    print(f"Wrote {out}  ({out.stat().st_size/1024:.0f} KB)")
    print(f"  inputs : {in_names}")
    print(f"  outputs: {out_names}")
    print("Done. Build with -DSATTARI_USE_ONNX=ON and enable Neural Pitch.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
