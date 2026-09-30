"""Tier-1 Auto Pitch accuracy on synthetic singers with exactly known pitch.

Each note is sung at target + offset (offset +-20..45 c, known exactly) with vibrato,
scoops, portamento, jitter and breath. A note passes when the centre of its core
(mean pitch from 100 ms after onset to 60 ms before the end) lands within TOL of the
target. `--oracle` scores a perfect per-note correction to validate the metric.

usage: synth_pitch.py make | score <tag> [bench args] | oracle | input
"""
import json, os, subprocess, sys
from pathlib import Path
import numpy as np, soundfile as sf
from synth_singer import sing, SR
from pitchref import load, f0

HERE = Path(__file__).parent; OUT = HERE / "synth_pitch"; OUT.mkdir(exist_ok=True)
BENCH = Path(os.environ.get("BENCH", str(HERE.parents[1] / "build/auto-pitch-neural/AutoPitchBench")))
RANGES = {"bass": (40, 55), "baritone": (45, 60), "tenor": (50, 67), "alto": (55, 70), "mezzo": (58, 74), "soprano": (62, 79)}
VOWELS = "aeiou"


def make():
    manifest = []
    for vi, (voice, (lo, hi)) in enumerate(RANGES.items()):
        for s in range(6):
            rng = np.random.default_rng(1000 * vi + s)
            notes, cur = [], rng.integers(lo + 3, hi - 3)
            for i in range(16):
                cur = int(np.clip(cur + rng.choice([-5, -4, -3, -2, -1, 1, 2, 3, 4, 5, 7]), lo, hi))
                off = float(rng.choice([-1, 1]) * rng.uniform(20, 45))
                notes.append((cur + off / 100.0, float(rng.uniform(0.35, 0.9)), VOWELS[rng.integers(5)],
                              0.0 if rng.random() < 0.4 else float(rng.uniform(0.08, 0.35))))
            y, midi, spans = sing(notes, voice=voice, seed=vi * 10 + s, vib_rate=float(rng.uniform(5.0, 6.5)),
                                  vib_depth_c=float(rng.uniform(15, 40)), scoop_c=float(rng.uniform(-70, -20)))
            name = f"{voice}_{s}"
            sf.write(OUT / f"{name}.wav", y, SR)
            manifest.append({"name": name, "voice": voice,
                             "notes": [{"start": a, "end": b, "target": int(round(c)), "offset_c": round(100 * (c - round(c)), 2)}
                                       for (a, b, c) in spans]})
    json.dump(manifest, open(OUT / "manifest.json", "w"), indent=1)
    print("made", len(manifest), "singers,", sum(len(m["notes"]) for m in manifest), "notes")


SUBSET = os.environ.get("SUBSET", "all")   # all | tune (singers _0.._2) | holdout (_3.._5)


def in_subset(name):
    k = int(name.rsplit("_", 1)[1])
    return SUBSET == "all" or (SUBSET == "tune" and k in (0, 1, 2)) or (SUBSET == "holdout" and k in (3, 4, 5))


def core_centres(midi10, notes):
    out = []
    for n in notes:
        a = int((n["start"] / SR + 0.10) * 100); b = int((n["end"] / SR - 0.06) * 100)
        seg = midi10[a:b]; seg = seg[np.isfinite(seg)]
        out.append(float(np.mean(seg)) if len(seg) >= 5 else float("nan"))
    return out


def crepe_cached(path, lat=0):
    c = Path(str(path) + ".crepe.npy")
    if c.exists() and c.stat().st_mtime >= Path(path).stat().st_mtime:
        return np.load(c)
    m, _ = f0(load(path)[lat:]); np.save(c, m); return m


def score(tag, get_midi, tol=10.0):
    man = json.load(open(OUT / "manifest.json")); errs = []; per_voice = {}
    for m in man:
        if not in_subset(m["name"]):
            continue
        centres = core_centres(get_midi(m["name"]), m["notes"])
        for n, c in zip(m["notes"], centres):
            if np.isfinite(c):
                e = 100 * (c - n["target"]); errs.append(e); per_voice.setdefault(m["voice"], []).append(e)
    a = np.abs(np.array(errs))
    summary = {"tag": tag, "subset": SUBSET, "notes": int(len(a)), "within_10c_pct": round(100 * float(np.mean(a <= tol)), 2),
               "within_5c_pct": round(100 * float(np.mean(a <= 5)), 2), "median_abs_c": round(float(np.median(a)), 2),
               "p95_abs_c": round(float(np.percentile(a, 95)), 2), "worst_c": round(float(a.max()), 1),
               "per_voice_within_10c": {v: round(100 * float(np.mean(np.abs(e) <= tol)), 1) for v, e in per_voice.items()}}
    json.dump(summary, open(OUT / f"score_{tag}.json", "w"), indent=1)
    print("SUMMARY", json.dumps(summary)); return summary


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "make":
        make()
    elif cmd == "input":
        score("input", lambda n: crepe_cached(OUT / f"{n}.wav"))
    elif cmd == "oracle":
        man = json.load(open(OUT / "manifest.json"))
        for m in man:
            lines = [f"{n['start']} {-n['offset_c'] / 100:.4f}" for n in m["notes"]]
            (OUT / f"{m['name']}_oracle.txt").write_text("\n".join(lines) + "\n")
            subprocess.run(["rubberband", "-3", "-F", "-q", "--pitchmap", str(OUT / f"{m['name']}_oracle.txt"),
                            str(OUT / f"{m['name']}.wav"), str(OUT / f"{m['name']}_oracle.wav")], check=True, capture_output=True)
        score("oracle", lambda n: crepe_cached(OUT / f"{n}_oracle.wav"))
    elif cmd == "score":
        from concurrent.futures import ThreadPoolExecutor
        tag, args = sys.argv[2], sys.argv[3:] or ["--style", "Classic"]
        man = json.load(open(OUT / "manifest.json")); lats = {}
        def run(m):
            outp = OUT / f"{m['name']}_{tag}.wav"; tr = OUT / f"{m['name']}_{tag}.json"
            # Private session bus per run: parallel instances (or an open DAW) must not
            # publish keys/state into each other's correction.
            env = dict(os.environ, SATTARI_SESSION_ID=f"acc-{tag}-{m['name']}-{os.getpid()}")
            subprocess.run([str(BENCH), str(OUT / f"{m['name']}.wav"), "--output", str(outp), "--trace", str(tr),
                            "--key", "C", "--scale", "chromatic", "--amount", "1", "--speed", "1", "--block", "512", *args],
                           check=True, capture_output=True, env=env)
            t = json.load(open(tr)); return m["name"], t["reportedLatencySamples"] + int(t.get("prototypeLookahead", 0))
        with ThreadPoolExecutor(8) as ex:
            lats = dict(ex.map(run, [m for m in man if in_subset(m["name"])]))
        score(tag, lambda n: crepe_cached(OUT / f"{n}_{tag}.wav", lats[n]))
