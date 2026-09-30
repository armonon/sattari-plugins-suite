"""Fast, accurate note-centre pitch for a single-singer track when note times and the
approximate pitch are known: harmonic-sum spectrum over the note core, searched +-60 c around
the target, with parabolic interpolation on a fine log-frequency grid."""
import numpy as np
SR = 48000


def centre_midi(y, s, e, target_midi, sr=SR, span_c=60.0):
    a, b = int((s + 0.08) * sr), int((e - 0.05) * sr)
    seg = y[a:b]
    if len(seg) < int(0.08 * sr):
        return float("nan")
    seg = seg * np.hanning(len(seg))
    n = 1 << int(np.ceil(np.log2(len(seg) * 8)))
    spec = np.abs(np.fft.rfft(seg, n)); df = sr / n
    cents = np.arange(-span_c, span_c + 0.01, 0.5)
    f0s = 440.0 * 2 ** ((target_midi - 69 + cents / 100.0) / 12)
    score = np.zeros_like(cents)
    for k in range(1, 9):
        idx = f0s * k / df
        i0 = np.floor(idx).astype(int); fr = idx - i0
        ok = i0 + 1 < len(spec)
        v = np.zeros_like(cents); v[ok] = (1 - fr[ok]) * spec[i0[ok]] + fr[ok] * spec[i0[ok] + 1]
        score += np.log1p(v / (np.median(spec) + 1e-12)) / k ** 0.5
    j = int(np.argmax(score))
    if 0 < j < len(cents) - 1:
        a1, b1, c1 = score[j - 1], score[j], score[j + 1]
        d = 0.5 * (a1 - c1) / (a1 - 2 * b1 + c1 + 1e-12)
    else:
        d = 0.0
    return target_midi + (cents[j] + d * 0.5) / 100.0
