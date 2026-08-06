# Bundled CREPE pitch model

`crepe.onnx` is a deterministic ONNX conversion of the original CREPE-tiny
weights published by the Music and Audio Research Laboratory. It is embedded in
the neural Auto Pitch binaries and is used only for monophonic pitch detection.

- Upstream: https://github.com/marl/crepe
- Weights commit: `bb29b8d99a89924476d112d27ed470e4ac5617c0`
- Upstream license: MIT (`CREPE-LICENSE.txt`)
- Original compressed SHA-256: `9c1d76d405674d2327f5636347d07f551dfd11c6acc0475b7fd85082cdab0add`
- Original HDF5 SHA-256: `409254d6a537cc432061112b7ec5f6fd283658f108fc2abc24db9c25858b42df`
- Converted ONNX SHA-256: `027006c98e649e4bd9c795fb84cb4df6f23d8a7a54255f38789c68e85b56bd60`

Regenerate and verify it with `scripts/fetch_crepe_onnx.py`. The conversion
tool requires `numpy`, `h5py`, and `onnx`; installing `onnxruntime` additionally
enables its inference smoke test.
