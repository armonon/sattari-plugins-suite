"""Auto Key accuracy corpus: synthetic singers performing melodies in known keys.

Every clip is composed from its key: a I-IV-V-I (major) or i-iv-V-i (harmonic-minor
cadence) progression, a melody drawn from the scale that leans on chord tones and
cadences on the tonic, sung by a synthetic singer (see synth_singer.py). Variants:
a cappella voice, and voice + pad chords + bass. Labels are exact by construction.

usage: synth_key.py make
"""
import json, sys
from pathlib import Path
import numpy as np, soundfile as sf
from synth_singer import sing, SR

HERE = Path(__file__).parent; OUT = HERE / "synth_key"; (OUT / "wav").mkdir(parents=True, exist_ok=True)
NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
MAJOR = [0, 2, 4, 5, 7, 9, 11]; NAT_MINOR = [0, 2, 3, 5, 7, 8, 10]
VOICE_RANGE = {"baritone": (48, 62), "tenor": (52, 67), "alto": (56, 70), "soprano": (62, 77)}


def chord(root_pc, degree, minor):
    """Triad pitch classes for scale degree 0 (I), 3 (IV), 4 (V)."""
    scale = NAT_MINOR if minor else MAJOR
    pcs = [(root_pc + scale[(degree + k) % 7]) % 12 for k in (0, 2, 4)]
    if minor and degree == 4:          # harmonic-minor dominant: raised leading tone
        pcs[1] = (root_pc + 11) % 12
    return pcs


def melody(root_pc, minor, voice, rng):
    lo, hi = VOICE_RANGE[voice]
    scale = NAT_MINOR if minor else MAJOR
    allowed = sorted(m for m in range(lo, hi + 1) if (m - root_pc) % 12 in scale)
    progression = [0, 3, 4, 0]
    notes, cur = [], min(allowed, key=lambda m: abs(m - (lo + hi) / 2))
    for bar, degree in enumerate(progression):
        tones = chord(root_pc, degree, minor)
        for beat in range(4):
            if bar == 3 and beat == 3:
                pool = [m for m in allowed if m % 12 == root_pc]          # final tonic
            else:
                pool = [m for m in allowed if abs(m - cur) <= 5] or allowed
                if minor and degree == 4:                                 # leading tone on V
                    lt = [m for m in range(lo, hi + 1) if m % 12 == (root_pc + 11) % 12 and abs(m - cur) <= 5]
                    pool = pool + lt
            w = np.array([3.0 if m % 12 in tones else 1.0 for m in pool])
            cur = int(rng.choice(pool, p=w / w.sum()))
            dur = 0.9 if (bar == 3 and beat == 3) else float(rng.choice([0.3, 0.45, 0.6]))
            notes.append((cur + rng.uniform(-0.06, 0.06), dur, "aeiou"[rng.integers(5)],
                          0.0 if rng.random() < 0.5 else 0.08))
    return notes, progression


KS_MAJ = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
KS_MIN = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])


def ks_agrees(notes, root_pc, minor, margin=0.05):
    h = np.zeros(12)
    for c, d, _, _ in notes:
        h[int(round(c)) % 12] += d
    scores = sorted(((np.corrcoef(h, np.roll(p, r))[0, 1], r, m) for r in range(12)
                     for m, p in ((False, KS_MAJ), (True, KS_MIN))), reverse=True)
    return scores[0][1] == root_pc and scores[0][2] == minor and scores[0][0] - scores[1][0] >= margin


def accompaniment(root_pc, minor, progression, total, bar_s):
    t = np.arange(total) / SR; out = np.zeros(total)
    for bar, degree in enumerate(progression):
        a = int((0.3 + bar * bar_s) * SR); b = min(total, int((0.3 + (bar + 1) * bar_s) * SR))
        if a >= total:
            break
        env = np.minimum(1, np.minimum((t[a:b] - t[a]) / 0.05, (t[b - 1] - t[a:b]) / 0.1 + 1e-3)).clip(0, 1)
        for i, pc in enumerate(chord(root_pc, degree, minor)):
            f = 440 * 2 ** ((48 + ((pc - 48) % 12) + 12 * (i > 0) - 69) / 12)
            out[a:b] += 0.12 * env * sum(np.sin(2 * np.pi * k * f * t[a:b]) / k ** 1.5 for k in range(1, 8))
        fb = 440 * 2 ** ((36 + (chord(root_pc, degree, minor)[0] - 36) % 12 - 69) / 12)
        out[a:b] += 0.18 * env * (np.sin(2 * np.pi * fb * t[a:b]) + 0.3 * np.sin(4 * np.pi * fb * t[a:b]))
    return out


def make():
    rows, meta = [], []
    for key_i in range(24):
        root_pc, minor = key_i % 12, key_i >= 12
        for vi, voice in enumerate(VOICE_RANGE):
            # Reject melodies whose own notes imply another key (Krumhansl-Schmuckler on
            # the duration-weighted pitch classes must pick the label with a clear margin),
            # so every label is musically unambiguous even a cappella.
            for attempt in range(500):
                rng = np.random.default_rng(key_i * 1000 + vi * 100 + attempt)
                notes, prog = melody(root_pc, minor, voice, rng)
                if ks_agrees(notes, root_pc, minor):
                    break
            y, _, _ = sing(notes, voice=voice, seed=key_i * 10 + vi, vib_depth_c=float(rng.uniform(10, 30)))
            label = f"{NAMES[root_pc]} {'minor' if minor else 'major'}"
            # a cappella for two voices, with accompaniment for the other two
            if vi % 2 == 1:
                bar_s = sum(d + r for _, d, _, r in notes) / 4
                acc = accompaniment(root_pc, minor, prog, len(y), bar_s)
                y = (0.7 * y + 0.5 * acc / (np.max(np.abs(acc)) + 1e-9) * 0.3).astype(np.float32)
                kind = "voice+chords"
            else:
                kind = "acappella"
            name = f"{NAMES[root_pc].replace('#', 's')}_{'min' if minor else 'maj'}_{voice}_{kind.replace('+', '_')}.wav"
            y = y / (np.max(np.abs(y)) + 1e-9) * 0.5
            sf.write(OUT / "wav" / name, y.astype(np.float32), SR)
            rows.append(f"{name},{label}"); meta.append({"file": name, "label": label, "voice": voice, "kind": kind})
    (OUT / "labels.csv").write_text("\n".join(rows) + "\n")
    json.dump(meta, open(OUT / "manifest.json", "w"), indent=1)
    print("made", len(rows), "clips")


if __name__ == "__main__":
    make()
