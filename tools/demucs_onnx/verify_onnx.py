#!/usr/bin/env python3
import sys, numpy as np, onnxruntime as ort

M = sys.argv[1] if len(sys.argv) > 1 else "neural-model.onnx"
so = ort.SessionOptions()
sess = ort.InferenceSession(M, so, providers=["CPUExecutionProvider"])
inp = sess.get_inputs()[0]; out = sess.get_outputs()[0]
print("input :", inp.name, inp.shape, inp.type)
print("output:", out.name, out.shape, out.type)
N = inp.shape[-1] if isinstance(inp.shape[-1], int) else 308224

rng = np.random.default_rng(0)
# a mildly musical-ish signal: sum of tones + noise
t = np.arange(N) / 44100.0
mix = (0.3*np.sin(2*np.pi*110*t) + 0.2*np.sin(2*np.pi*440*t)
       + 0.1*rng.standard_normal(N)).astype(np.float32)
x = np.stack([mix, mix*0.9])[None]                     # [1,2,N]
y = sess.run([out.name], {inp.name: x.astype(np.float32)})[0]
print("ort out shape:", y.shape, "finite:", bool(np.isfinite(y).all()))
# Demucs property: sum of the 4 stems ~= input mix.
ssum = y.sum(axis=1)                                    # [1,2,N]
err = np.abs(ssum - x).max()
denom = np.abs(x).max() + 1e-9
print(f"stems-sum vs mix: max|err|={err:.4e}  rel={err/denom:.4e}")
# per-stem energy (drums,bass,other,vocals)
for i,name in enumerate(["drums","bass","other","vocals"]):
    e = float(np.sqrt((y[0,i]**2).mean()))
    print(f"  {name:7s} rms={e:.5f}")
print("ONNX_OK" if (np.isfinite(y).all() and err/denom < 0.05 and y.shape[1]==4) else "ONNX_FAIL")
