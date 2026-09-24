"""Auto Pitch note-level accuracy on generated voices.

For every voice: each held note (CREPE reference) is detuned by its own known offset
(+-20..45 c, never past the half-semitone so the intended note stays unambiguous)
with rubberband --pitchmap. The detuned file goes through the current Auto Pitch
processor (AutoPitchBench, DAW-like 512-sample blocks). A note PASSES when the
median pitch of its core (skip 80 ms onset / 50 ms tail) lands within TOL cents of
the intended note.

usage: accuracy_pitch.py <tag> [bench args...]   e.g. accuracy_pitch.py classic --style Classic
"""
import json, subprocess, sys
from pathlib import Path
import numpy as np, soundfile as sf
from pitchref import load, f0, SR

HERE = Path(__file__).parent
import os
BENCH = Path(os.environ.get("BENCH", str(HERE.parents[1] / "build-live-integration/plugins/auto-pitch/AutoPitchBench")))
TOL = 10.0
tag = sys.argv[1]
bench_args = sys.argv[2:] or ["--style", "Classic"]
REF = json.load(open(HERE / "voices/reference_notes.json"))
work = HERE / "work"; work.mkdir(exist_ok=True)
outdir = HERE / "results"; outdir.mkdir(exist_ok=True)
rng = np.random.default_rng(20260923)


def detuned(vid, notes):
    """Per-note detune via rubberband pitch map; cached so every run sees the same input."""
    wav = work / f"{vid}_detuned.wav"; plan = work / f"{vid}_detune.json"
    if wav.exists() and plan.exists():
        return wav, json.load(open(plan))
    offsets, lines = [], []
    for (a, b, tgt, med) in notes:
        singer = 100 * (med - tgt)
        while True:
            d = float(rng.choice([-1, 1]) * rng.uniform(20, 45))
            if abs(singer + d) <= 45:
                break
        offsets.append(d)
        lines.append(f"{int(a * 0.01 * SR)} {d / 100:.4f}")
    (work / f"{vid}_pitchmap.txt").write_text("\n".join(lines) + "\n")
    subprocess.run(["rubberband", "-3", "-F", "-q", "--pitchmap", str(work / f"{vid}_pitchmap.txt"),
                    str(HERE / "voices" / f"{vid}.wav"), str(wav)], check=True, capture_output=True)
    json.dump(offsets, open(plan, "w"))
    return wav, offsets


def note_errors(midi, notes, shift_frames=0):
    errs = []
    for (a, b, tgt, _) in notes:
        s, e = a + 8 + shift_frames, b - 5 + shift_frames
        seg = midi[max(0, s):max(0, e)]
        seg = seg[np.isfinite(seg)]
        errs.append(float(100 * (np.median(seg) - tgt)) if len(seg) >= 3 else float("nan"))
    return errs


rows, all_in, all_out = [], [], []
import os
SUBSET = os.environ.get("VOICES", "all")   # all | tune (odd voices) | holdout (even voices)
def in_subset(vid):
    k = int(vid[1:3])
    return SUBSET == "all" or (SUBSET == "tune" and k % 2 == 1) or (SUBSET == "holdout" and k % 2 == 0)
def cached_f0(path):
    cache = Path(str(path) + ".crepe.npy")
    if cache.exists() and cache.stat().st_mtime >= Path(path).stat().st_mtime:
        return np.load(cache)
    m, _ = f0(load(path)); np.save(cache, m); return m
for vid, r in sorted(REF.items()):
    notes = [tuple(n) for n in r["notes"]]
    if not notes or not in_subset(vid):
        continue
    src_wav, offsets = detuned(vid, notes)
    out_wav = work / f"{vid}_{tag}.wav"; trace = work / f"{vid}_{tag}.json"
    subprocess.run([str(BENCH), str(src_wav), "--output", str(out_wav), "--trace", str(trace),
                    "--key", "C", "--scale", "chromatic", "--amount", "1", "--speed", "1", "--block", "512",
                    *bench_args], check=True, capture_output=True)
    lat = int(json.load(open(trace)).get("reportedLatencySamples", 0))
    y = load(out_wav)[lat:]
    m_out, _ = f0(y)
    m_in = cached_f0(src_wav)
    e_in = note_errors(m_in, notes); e_out = note_errors(m_out, notes)
    ok = [abs(e) <= TOL for e in e_out if np.isfinite(e)]
    rows.append({"voice": vid, "notes": len(notes), "measured": len(ok), "pass": int(sum(ok)),
                 "in_median_abs_c": round(float(np.nanmedian(np.abs(e_in))), 1),
                 "out_median_abs_c": round(float(np.nanmedian(np.abs(e_out))), 1),
                 "out_worst_c": round(float(np.nanmax(np.abs(e_out))), 1),
                 "failures": [{"note": i, "target_midi": notes[i][2], "err_c": round(e, 1), "detune_c": round(offsets[i], 1)}
                              for i, e in enumerate(e_out) if np.isfinite(e) and abs(e) > TOL]})
    all_in += e_in; all_out += e_out
    x = rows[-1]
    print(f"{vid:30} pass {x['pass']:3d}/{x['measured']:3d} | in {x['in_median_abs_c']:5.1f}c -> out {x['out_median_abs_c']:5.1f}c | worst {x['out_worst_c']:5.1f}c", flush=True)

o = np.array([e for e in all_out if np.isfinite(e)])
summary = {"tag": tag, "voices": SUBSET, "bench_args": bench_args, "tolerance_c": TOL, "notes_measured": int(len(o)),
           "notes_pass": int(np.sum(np.abs(o) <= TOL)), "accuracy_pct": round(100 * float(np.mean(np.abs(o) <= TOL)), 2),
           "within_5c_pct": round(100 * float(np.mean(np.abs(o) <= 5)), 2),
           "median_abs_c": round(float(np.median(np.abs(o))), 2), "p95_abs_c": round(float(np.percentile(np.abs(o), 95)), 2)}
json.dump({"summary": summary, "voices": rows}, open(outdir / f"pitch_{tag}.json", "w"), indent=1)
print("SUMMARY", json.dumps(summary))
