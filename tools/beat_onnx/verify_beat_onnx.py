#!/usr/bin/env python3
"""Sanity-check an exported beat-model ONNX: synthesize a click track at a known
BPM, run the model, peak-pick the beat activation, and report detected BPM and
how tightly the picked beats land on the true clicks. No ground-truth dataset
needed — this just proves the exported graph runs and localizes beats.

  python verify_beat_onnx.py beat-model.onnx --bpm 128
"""
import argparse
import numpy as np

SAMPLE_RATE = 22050
N_FFT = 1024
HOP = 441
N_MELS = 128
FMIN, FMAX = 30.0, 11025.0


def mel_filterbank(sr, n_fft, n_mels, fmin, fmax):
    def hz2mel(f):
        return 2595.0 * np.log10(1.0 + f / 700.0)

    def mel2hz(m):
        return 700.0 * (10.0 ** (m / 2595.0) - 1.0)

    mpts = np.linspace(hz2mel(fmin), hz2mel(fmax), n_mels + 2)
    hz = mel2hz(mpts)
    bins = np.floor((n_fft + 1) * hz / sr).astype(int)
    fb = np.zeros((n_mels, n_fft // 2 + 1), np.float32)
    for m in range(1, n_mels + 1):
        l, c, r = bins[m - 1], bins[m], bins[m + 1]
        for k in range(l, c):
            if c > l:
                fb[m - 1, k] = (k - l) / (c - l)
        for k in range(c, r):
            if r > c:
                fb[m - 1, k] = (r - k) / (r - c)
    return fb


def log_mel(x):
    fb = mel_filterbank(SAMPLE_RATE, N_FFT, N_MELS, FMIN, FMAX)
    win = np.hanning(N_FFT).astype(np.float32)
    frames = []
    for s in range(0, len(x) - N_FFT, HOP):
        spec = np.abs(np.fft.rfft(x[s:s + N_FFT] * win)) ** 2
        frames.append(fb @ spec)
    m = np.log1p(np.stack(frames, 0)).astype(np.float32)   # [T, n_mels]
    return m[None]                                          # [1, T, n_mels]


def click_track(bpm, secs):
    n = int(SAMPLE_RATE * secs)
    x = np.zeros(n, np.float32)
    step = SAMPLE_RATE * 60.0 / bpm
    t = 0.0
    while t < n:
        s = int(t)
        i = np.arange(min(int(SAMPLE_RATE * 0.02), n - s))
        x[s:s + len(i)] += np.exp(-i / (SAMPLE_RATE * 0.004)) * np.sin(2 * np.pi * 2000 * i / SAMPLE_RATE)
        t += step
    return x


def peak_pick(act, fps, min_bpm=60, max_bpm=200):
    min_gap = int(fps * 60.0 / max_bpm)
    peaks = []
    for i in range(1, len(act) - 1):
        if act[i] > 0.3 and act[i] >= act[i - 1] and act[i] > act[i + 1]:
            if not peaks or i - peaks[-1] >= min_gap:
                peaks.append(i)
    return np.array(peaks) / fps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("--bpm", type=float, default=128.0)
    args = ap.parse_args()

    import onnxruntime as ort
    x = click_track(args.bpm, 20.0)
    mel = log_mel(x)
    sess = ort.InferenceSession(args.model, providers=["CPUExecutionProvider"])
    beat, down = sess.run(["beat", "down"], {"mel": mel})
    fps = SAMPLE_RATE / HOP
    times = peak_pick(beat[0], fps)
    if len(times) < 3:
        print("FAIL: model produced too few beats"); return
    iois = np.diff(times)
    det = 60.0 / np.median(iois)
    err = abs(det - args.bpm)
    print(f"true {args.bpm:.1f} BPM  ->  detected {det:.2f} BPM  ({len(times)} beats)  err {err:.2f}")
    print("BEAT_ONNX_OK" if err < 3.0 else "BEAT_ONNX_FAIL")


if __name__ == "__main__":
    main()
