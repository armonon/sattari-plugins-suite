"""Auto Pitch accuracy on REAL singers: Choral Singing Dataset (Cuesta et al., CC BY 4.0,
zenodo.org/records/2649950). 16 professional choir singers recorded individually; per-section
note annotations and per-track f0, both manually corrected (Tony).

Target of each note = nearest equal-tempered semitone of the section's annotated note pitch
(the intended score note; cross-checked against the section MIDI). A note passes when the mean
of its core (onset + 80 ms .. end - 50 ms) in the output lands within 10 c of the target.

Modes: natural = the singers' own intonation; detuned = each note pushed a further +-20..45 c.
Splits (by SINGER, csd_split.json): dev = singers 1-3 of each section (12), holdout = singer 4 of each section (4).

usage: csd_bench.py prep | score <tag> <natural|detuned> <tune|holdout|all> [bench args] | rescore <tag> <mode> <split> | oracle <mode> <split>
"""
import json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np, soundfile as sf
from pitchref import load, f0, SR
from diskguard import require_free

HERE = Path(__file__).parent
DS = Path.home() / "datasets/choral_singing/ChoralSingingDataset"
WORK = HERE / "csd_work"; WORK.mkdir(exist_ok=True)
BENCH = Path(os.environ.get("BENCH", str(HERE.parents[1] / "build/auto-pitch-neural/AutoPitchBench")))
TOL = 10.0


def midi(hz):
    return 69 + 12 * np.log2(np.asarray(hz, float) / 440.0)


SPLIT = json.load(open(HERE / "csd_split.json"))   # split by SINGER (see csd_split.json)


def tracks(split):
    """dev = singers 1-3 of each section; holdout = singer 4 of each section (never tuned on)."""
    if split in ("dev", "tune"):
        return list(SPLIT["dev_tracks"])
    if split == "holdout":
        return list(SPLIT["holdout_tracks"])
    return sorted(SPLIT["dev_tracks"] + SPLIT["holdout_tracks"])


_score_cache = {}


def score_pitches(piece, section):
    """Pitches present in the section's MIDI score (the intended notes)."""
    key = (piece, section)
    if key not in _score_cache:
        import mido
        mid = mido.MidiFile(DS / f"CSD_{piece}_{section}_midi.mid")
        _score_cache[key] = {m.note for tr in mid.tracks for m in tr if m.type == "note_on" and m.velocity > 0}
    return _score_cache[key]


def section_notes(stem):
    """Annotated notes whose rounded pitch is a note of the score (96% of them); the rest sit
    on a semitone boundary where the intended note can't be read off the annotation."""
    _, piece, section, _ = stem.split("_")
    allowed = score_pitches(piece, section)
    rows = [l.split() for l in open(DS / f"CSD_{piece}_{section}_notes.lab") if l.strip()]
    notes = [(float(s), float(s) + float(d), int(round(midi(float(f))))) for s, f, d in rows if float(f) > 0]
    return [n for n in notes if n[2] in allowed]


def singer_f0(stem):
    a = np.loadtxt(DS / f"{stem}.f0")
    t, hz = a[:, 0], a[:, 1]
    return t, np.where(hz > 0, midi(np.maximum(hz, 1e-6)), np.nan)


def note_centre(t, m, s, e):
    sel = (t >= s + 0.08) & (t <= e - 0.05) & np.isfinite(m)
    return float(np.mean(m[sel])) if sel.sum() >= 5 else float("nan")


def prep():
    for stem in tracks("all"):
        wav48 = WORK / f"{stem}.wav"
        if not wav48.exists():
            subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(DS / f"{stem}.wav"), "-ac", "1", "-ar", str(SR), str(wav48)], check=True)
        # detuned variant: every note pushed by its own offset, keeping the intended note nearest
        rng = np.random.default_rng(abs(hash(stem)) % (2 ** 32))
        t, m = singer_f0(stem); lines = []
        for s, e, tgt in section_notes(stem):
            c = note_centre(t, m, s, e)
            if not np.isfinite(c):
                continue
            own = 100 * (c - tgt)
            for _ in range(200):
                d = float(rng.choice([-1, 1]) * rng.uniform(20, 45))
                if abs(own + d) <= 45:
                    break
            lines.append(f"{int(s * SR)} {d / 100:.4f}")
            lines.append(f"{int(e * SR)} 0")
        (WORK / f"{stem}_detune.txt").write_text("\n".join(lines) + "\n")
        det = WORK / f"{stem}_detuned.wav"
        if not det.exists():
            subprocess.run(["rubberband", "-3", "-F", "-q", "--pitchmap", str(WORK / f"{stem}_detune.txt"), str(wav48), str(det)],
                           check=True, capture_output=True)
    print("prepared", len(tracks("all")), "tracks")


def crepe_cached(path, lat=0):
    # CREPE-tiny: within 2.5 c (median) of the manually corrected annotations, ~50x faster than full.
    # ".s0": reference computed with the fixed dither seed (see pitchref.f0); older unseeded
    # caches are not reproducible and are not reused.
    c = Path(str(path) + f".lat{lat}.crepetiny.s0.npy")
    if c.exists():
        return np.load(c)
    m, _ = f0(load(path)[lat:], model="tiny"); np.save(c, m); return m


def score_outputs(tag, mode, split, get_midi):
    errs_in, errs_out = [], []
    for stem in tracks(split):
        notes = section_notes(stem)
        t, m_ref = singer_f0(stem)
        m_in = crepe_cached(WORK / (f"{stem}.wav" if mode == "natural" else f"{stem}_detuned.wav"))
        m_out = get_midi(stem)
        tt = np.arange(len(m_out)) * 0.01; ti = np.arange(len(m_in)) * 0.01
        for s, e, tgt in notes:
            ci = note_centre(ti, m_in, s, e); co = note_centre(tt, m_out, s, e)
            if not (np.isfinite(ci) and np.isfinite(co)) or abs(100 * (ci - tgt)) > 48:
                continue          # skip notes whose input is already ambiguous
            errs_in.append(100 * (ci - tgt)); errs_out.append(100 * (co - tgt))
    a, b = np.abs(errs_in), np.abs(errs_out)
    summary = {"tag": tag, "mode": mode, "split": split, "notes": int(len(b)),
               "input_within_10c": round(100 * float(np.mean(a <= TOL)), 1), "input_median_c": round(float(np.median(a)), 1),
               "output_within_10c": round(100 * float(np.mean(b <= TOL)), 1), "output_within_5c": round(100 * float(np.mean(b <= 5)), 1),
               "output_median_c": round(float(np.median(b)), 2), "output_p95_c": round(float(np.percentile(b, 95)), 1)}
    json.dump(summary, open(WORK / f"score_{tag}_{mode}_{split}.json", "w"), indent=1)
    print("SUMMARY", json.dumps(summary)); return summary


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "prep":
        prep()
    elif cmd == "oracle":
        mode, split = sys.argv[2], sys.argv[3]
        for stem in tracks(split):
            src = WORK / (f"{stem}.wav" if mode == "natural" else f"{stem}_detuned.wav")
            m_in = crepe_cached(src); ti = np.arange(len(m_in)) * 0.01; lines = []
            for s, e, tgt in section_notes(stem):
                c = note_centre(ti, m_in, s, e)
                if np.isfinite(c):
                    lines += [f"{int(s * SR)} {-(c - tgt):.4f}", f"{int(e * SR)} 0"]
            (WORK / f"{stem}_{mode}_oracle.txt").write_text("\n".join(lines) + "\n")
            subprocess.run(["rubberband", "-3", "-F", "-q", "--pitchmap", str(WORK / f"{stem}_{mode}_oracle.txt"), str(src),
                            str(WORK / f"{stem}_{mode}_oracle.wav")], check=True, capture_output=True)
        score_outputs("oracle", mode, split, lambda st: crepe_cached(WORK / f"{st}_{mode}_oracle.wav"))
    elif cmd == "rescore":   # score existing renders of <tag> again (e.g. with CREPE_DEVICE=cpu)
        tag, mode, split = sys.argv[2], sys.argv[3], sys.argv[4]
        lats = {st: json.load(open(WORK / f"{st}_{mode}_{tag}.json"))["reportedLatencySamples"] for st in tracks(split)}
        score_outputs(tag, mode, split, lambda st: crepe_cached(WORK / f"{st}_{mode}_{tag}.wav", lats[st]))
    elif cmd == "score":
        tag, mode, split, args = sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5:] or ["--style", "Classic"]
        require_free()
        def run(stem):
            src = WORK / (f"{stem}.wav" if mode == "natural" else f"{stem}_detuned.wav")
            out = WORK / f"{stem}_{mode}_{tag}.wav"; tr = WORK / f"{stem}_{mode}_{tag}.json"
            if out.exists(): out.unlink()
            env = dict(os.environ, SATTARI_SESSION_ID=f"csd-{tag}-{stem}-{os.getpid()}")
            # CSD_KEYED=1: correct each piece in its own key (csd_keys.json, from the score) instead of chromatic.
            key, scale = "C", "chromatic"
            if os.environ.get("CSD_KEYED") == "1":
                k = json.load(open(HERE / "csd_keys.json"))[stem.split("_")[1]]; key, scale = k["root"], k["scale"]
            subprocess.run([str(BENCH), str(src), "--output", str(out), "--trace", str(tr), "--key", key, "--scale", scale,
                            "--amount", "1", "--speed", "1", "--block", "512", *args], check=True, capture_output=True, env=env)
            return stem, json.load(open(tr))["reportedLatencySamples"]
        with ThreadPoolExecutor(8) as ex:
            lats = dict(ex.map(run, tracks(split)))
        score_outputs(tag, mode, split, lambda st: crepe_cached(WORK / f"{st}_{mode}_{tag}.wav", lats[st]))
        # Renders are deleted once scored (~26 MB each, 48 per tag filled the disk on 2026-09-25).
        # Traces and the CREPE caches stay, so the score is still reproducible. CSD_KEEP=1 keeps them.
        if os.environ.get("CSD_KEEP") != "1":
            for st in tracks(split):
                (WORK / f"{st}_{mode}_{tag}.wav").unlink(missing_ok=True)
