"""Synthetic singing voice with an exactly known pitch contour.

Source-filter model done additively: every harmonic of a glottal-like source is
weighted by the vowel's formant envelope evaluated at that harmonic's frequency,
so formants stay put while pitch moves (like a real voice). Adds vibrato with a
delayed onset, onset scoops, portamento, jitter, shimmer, aspiration noise and
breaths. Returns the audio and the true per-sample MIDI contour.
"""
import numpy as np

SR = 48000

VOICES = {  # name: (formant scale, spectral tilt dB/oct, breathiness)
    "bass": (0.86, -11.0, 0.010), "baritone": (0.92, -11.0, 0.012), "tenor": (1.00, -10.0, 0.012),
    "alto": (1.10, -11.5, 0.018), "mezzo": (1.14, -11.0, 0.015), "soprano": (1.20, -12.0, 0.020),
}
VOWELS = {  # F1..F4 (Hz) and bandwidths, adult male reference
    "a": ([730, 1090, 2440, 3400], [90, 110, 160, 250]),
    "e": ([530, 1840, 2480, 3500], [70, 100, 160, 250]),
    "i": ([290, 2250, 2890, 3600], [60, 100, 170, 250]),
    "o": ([570, 840, 2410, 3400], [80, 100, 160, 250]),
    "u": ([300, 870, 2240, 3300], [60, 90, 150, 250]),
}


def formant_gain(freqs, vowel, scale):
    fs, bws = VOWELS[vowel]
    g = np.zeros_like(freqs)
    for f, bw in zip(fs, bws):
        f = f * scale
        g += 1.0 / np.sqrt(1.0 + ((freqs - f) / (bw / 2.0)) ** 2)
    return g


def sing(notes, voice="tenor", seed=0, vib_rate=5.6, vib_depth_c=30.0, scoop_c=-45.0, tempo_scale=1.0):
    """notes: list of (midi_float_centre, dur_s, vowel, rest_after_s). midi_float_centre is
    the exact sung centre (e.g. 60.35 = C4 + 35 c). Returns (audio, true_midi_per_sample, spans)
    where spans = [(start_sample, end_sample, centre_midi)] for each note."""
    rng = np.random.default_rng(seed)
    scale, tilt, breath = VOICES[voice]
    total = int(SR * (0.3 + sum(d + r for _, d, _, r in notes) * tempo_scale + 0.4))
    midi = np.full(total, np.nan); amp = np.zeros(total); vowel_at = np.empty(total, dtype=object)
    pos = int(0.3 * SR); spans = []; prev = None
    for centre, dur, vowel, rest in notes:
        n = int(dur * tempo_scale * SR); t = np.arange(n) / SR
        contour = np.full(n, centre, dtype=float)
        # onset: scoop from below (or portamento from the previous note if legato)
        glide = int(0.08 * SR)
        start_from = prev if (prev is not None and rest_prev == 0.0) else centre + scoop_c / 100.0
        k = np.minimum(np.arange(n) / glide, 1.0)
        contour += (start_from - centre) * (1 - k) ** 2
        # vibrato fades in after 150 ms
        fade = np.clip((t - 0.15) / 0.25, 0, 1)
        rate = vib_rate * (1 + 0.04 * rng.standard_normal())
        contour += fade * (vib_depth_c / 100.0) * np.sin(2 * np.pi * rate * t + rng.uniform(0, 6.28))
        # slow drift + jitter
        contour += 0.03 * np.sin(2 * np.pi * 0.7 * t + rng.uniform(0, 6.28)) + 0.004 * rng.standard_normal(n)
        env = np.minimum(1, np.minimum(t / 0.03, (dur * tempo_scale - t) / 0.06)).clip(0, 1)
        env *= 1 + 0.05 * np.sin(2 * np.pi * 3.1 * t)          # shimmer-ish
        midi[pos:pos + n] = contour; amp[pos:pos + n] = env; vowel_at[pos:pos + n] = vowel
        spans.append((pos, pos + n, centre))
        pos += n + int(rest * tempo_scale * SR); prev = centre; rest_prev = rest
    voiced = np.isfinite(midi)
    f0 = np.where(voiced, 440.0 * 2 ** ((np.nan_to_num(midi, nan=60) - 69) / 12), 0.0)
    phase = 2 * np.pi * np.cumsum(f0) / SR
    out = np.zeros(total)
    # additive synthesis, formant-weighted per harmonic; process in 20 ms chunks for vowel/f0 changes
    hop = 960
    for s in range(0, total, hop):
        e = min(total, s + hop)
        if not voiced[s:e].any():
            continue
        v = next((x for x in vowel_at[s:e] if x is not None), "a")
        f_mean = float(np.mean(f0[s:e][f0[s:e] > 0]))
        kmax = int(min(60, 7000 // f_mean))
        ks = np.arange(1, kmax + 1)
        w = formant_gain(ks * f_mean, v, scale) * (ks ** (tilt / 6.02)) / np.maximum(1, ks) ** 0.0
        w = w / (np.sqrt(np.sum(w ** 2)) + 1e-9)
        out[s:e] = np.sum(w[:, None] * np.sin(ks[:, None] * phase[None, s:e]), axis=0)
    noise = rng.standard_normal(total) * breath
    out = (out + noise * (amp > 0)) * amp
    out += rng.standard_normal(total) * 0.0008                    # room floor
    out = 0.3 * out / (np.max(np.abs(out)) + 1e-9)
    return out.astype(np.float32), midi, spans
