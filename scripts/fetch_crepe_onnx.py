#!/usr/bin/env python3
"""Build Sattari's bundled CREPE-tiny ONNX model from the original MIT weights.

The converter deliberately does not download an arbitrary ONNX export. It fetches
the original marl/crepe `models` branch at a pinned commit, verifies the compressed
and decompressed weights, reconstructs the published Keras inference graph, and
writes a deterministic ONNX model with provenance embedded in its metadata.

Dependencies (tooling only, not shipped): numpy, h5py, onnx.
"""
from __future__ import annotations

import argparse
import bz2
import hashlib
import math
import tempfile
import urllib.request
from pathlib import Path

SOURCE_COMMIT = "bb29b8d99a89924476d112d27ed470e4ac5617c0"
SOURCE_URL = (
    "https://raw.githubusercontent.com/marl/crepe/"
    f"{SOURCE_COMMIT}/model-tiny.h5.bz2"
)
SOURCE_BZ2_SHA256 = "9c1d76d405674d2327f5636347d07f551dfd11c6acc0475b7fd85082cdab0add"
SOURCE_H5_SHA256 = "409254d6a537cc432061112b7ec5f6fd283658f108fc2abc24db9c25858b42df"
OUTPUT_SHA256 = "027006c98e649e4bd9c795fb84cb4df6f23d8a7a54255f38789c68e85b56bd60"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_hash(path: Path, expected: str, label: str) -> None:
    actual = sha256(path)
    if actual != expected:
        raise RuntimeError(f"{label} SHA-256 mismatch: expected {expected}, got {actual}")


def add_float(initializers: list, name: str, value) -> None:
    import numpy as np
    from onnx import numpy_helper

    initializers.append(
        numpy_helper.from_array(np.asarray(value, dtype=np.float32), name)
    )


def convert(h5_path: Path, output: Path) -> None:
    import h5py
    import numpy as np
    import onnx
    from onnx import TensorProto, helper, numpy_helper

    nodes = []
    initializers = [
        numpy_helper.from_array(np.array([0, 1, 1024, 1], dtype=np.int64), "input_shape")
    ]
    nodes.append(helper.make_node("Reshape", ["input", "input_shape"], ["x0"], name="input-reshape"))

    with h5py.File(h5_path, "r") as weights:
        previous = "x0"
        for index in range(1, 7):
            conv_group = weights[f"conv{index}"]
            conv_data = conv_group[next(iter(conv_group.keys()))]
            # Keras stores HWIO; ONNX Conv consumes OIHW.
            kernel = np.asarray(conv_data["kernel:0"]).transpose(3, 2, 0, 1)
            bias = np.asarray(conv_data["bias:0"])
            add_float(initializers, f"conv{index}.weight", kernel)
            add_float(initializers, f"conv{index}.bias", bias)

            conv = f"conv{index}.out"
            relu = f"conv{index}.relu"
            batch_norm = f"conv{index}.bn"
            pool = f"conv{index}.pool"
            stride = [4, 1] if index == 1 else [1, 1]
            nodes.append(
                helper.make_node(
                    "Conv",
                    [previous, f"conv{index}.weight", f"conv{index}.bias"],
                    [conv],
                    name=f"conv{index}",
                    strides=stride,
                    auto_pad="SAME_UPPER",
                )
            )
            # The reference graph applies the Conv2D activation before BatchNorm.
            nodes.append(helper.make_node("Relu", [conv], [relu], name=f"conv{index}-relu"))

            bn_group = weights[f"conv{index}-BN"]
            bn_data = bn_group[next(iter(bn_group.keys()))]
            for source, suffix in (
                ("gamma:0", "scale"),
                ("beta:0", "bias"),
                ("moving_mean:0", "mean"),
                ("moving_variance:0", "var"),
            ):
                add_float(initializers, f"bn{index}.{suffix}", np.asarray(bn_data[source]))
            nodes.append(
                helper.make_node(
                    "BatchNormalization",
                    [
                        relu,
                        f"bn{index}.scale",
                        f"bn{index}.bias",
                        f"bn{index}.mean",
                        f"bn{index}.var",
                    ],
                    [batch_norm],
                    name=f"conv{index}-BN",
                    epsilon=0.001,
                )
            )
            nodes.append(
                helper.make_node(
                    "MaxPool",
                    [batch_norm],
                    [pool],
                    name=f"conv{index}-maxpool",
                    kernel_shape=[2, 1],
                    strides=[2, 1],
                )
            )
            previous = pool

        # NCHW -> Keras Permute((2,1,3)) order, then the reference Dense head.
        nodes.append(
            helper.make_node(
                "Transpose", [previous], ["transposed"], name="transpose", perm=[0, 3, 2, 1]
            )
        )
        nodes.append(helper.make_node("Flatten", ["transposed"], ["flat"], name="flatten", axis=1))

        classifier_group = weights["classifier"]
        classifier = classifier_group[next(iter(classifier_group.keys()))]
        add_float(initializers, "classifier.weight", np.asarray(classifier["kernel:0"]))
        add_float(initializers, "classifier.bias", np.asarray(classifier["bias:0"]))
        nodes.append(
            helper.make_node(
                "MatMul", ["flat", "classifier.weight"], ["logits0"], name="classifier-matmul"
            )
        )
        nodes.append(
            helper.make_node(
                "Add", ["logits0", "classifier.bias"], ["logits"], name="classifier-bias"
            )
        )
        nodes.append(helper.make_node("Sigmoid", ["logits"], ["salience"], name="classifier"))

    graph = helper.make_graph(
        nodes,
        "CREPE tiny pitch tracker",
        [helper.make_tensor_value_info("input", TensorProto.FLOAT, ["batch", 1024])],
        [helper.make_tensor_value_info("salience", TensorProto.FLOAT, ["batch", 360])],
        initializer=initializers,
    )
    model = helper.make_model(
        graph,
        producer_name="Sattari Audio reproducible CREPE converter",
        opset_imports=[helper.make_opsetid("", 17)],
    )
    model.ir_version = 8
    model.metadata_props.add(key="source_repository", value="https://github.com/marl/crepe")
    model.metadata_props.add(key="source_commit", value=SOURCE_COMMIT)
    model.metadata_props.add(key="source_weights_sha256", value=SOURCE_H5_SHA256)
    model.metadata_props.add(key="license", value="MIT")
    onnx.checker.check_model(model)
    output.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(model, output)


def smoke_with_onnxruntime(output: Path) -> None:
    try:
        import numpy as np
        import onnxruntime as ort
    except ImportError:
        return
    session = ort.InferenceSession(str(output), providers=["CPUExecutionProvider"])
    for frequency in (65.406, 110.0, 220.0, 440.0, 880.0):
        time = np.arange(1024, dtype=np.float32) / 16000.0
        frame = np.sin(2.0 * np.pi * frequency * time).astype(np.float32)
        frame = (frame - frame.mean()) / max(float(frame.std()), 1.0e-8)
        salience = session.run(None, {"input": frame[None, :]})[0][0]
        center = int(np.argmax(salience))
        start, end = max(0, center - 4), min(360, center + 5)
        bins = np.arange(start, end)
        cents = 1997.3794084376191 + 20.0 * bins
        decoded = float(10.0 * 2.0 ** (np.sum(salience[start:end] * cents) / np.sum(salience[start:end]) / 1200.0))
        error = 1200.0 * math.log2(decoded / frequency)
        if abs(error) > 15.0:
            raise RuntimeError(f"CREPE smoke failed at {frequency} Hz: {error:+.2f} cents")


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=repo / "plugins/auto-pitch/models/crepe.onnx",
    )
    parser.add_argument("--check", action="store_true", help="only verify the committed output")
    args = parser.parse_args()
    output = args.output.expanduser().resolve()

    if args.check:
        require_hash(output, OUTPUT_SHA256, "bundled CREPE ONNX")
        try:
            import onnx
        except ImportError:
            print("onnx is not installed; SHA-256 verification is complete, structural check skipped")
        else:
            onnx.checker.check_model(onnx.load(output))
        smoke_with_onnxruntime(output)
        print(f"CREPE model verified: {output} ({OUTPUT_SHA256})")
        return 0

    with tempfile.TemporaryDirectory(prefix="sattari-crepe-") as temp:
        compressed = Path(temp) / "model-tiny.h5.bz2"
        h5_path = Path(temp) / "model-tiny.h5"
        urllib.request.urlretrieve(SOURCE_URL, compressed)
        require_hash(compressed, SOURCE_BZ2_SHA256, "CREPE compressed source")
        h5_path.write_bytes(bz2.decompress(compressed.read_bytes()))
        require_hash(h5_path, SOURCE_H5_SHA256, "CREPE source weights")
        convert(h5_path, output)

    require_hash(output, OUTPUT_SHA256, "generated CREPE ONNX")
    smoke_with_onnxruntime(output)
    print(f"CREPE model generated: {output} ({OUTPUT_SHA256})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
