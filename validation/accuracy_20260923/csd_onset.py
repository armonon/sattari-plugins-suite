"""Onset-resolved diagnosis of Auto Pitch on real singers (Choral Singing Dataset).

For every ground-truth note, each 10 ms frame from the annotated onset is compared across:
  input  = the singer (CREPE-tiny on the raw track)
  output = Auto Pitch (CREPE-tiny on the latency-trimmed render)
  oracle = perfect per-note correction (constant shift of each note to its target)
and Auto Pitch's own state from its trace (chosen target, detected pitch, confidence, voicing).

Failure modes are separated per frame:
  A  wrong target   : Auto Pitch aimed at a different note than the ground truth
  B  right target, output still > 10 c off (magnitude / timing)

usage: csd_onset.py <tag> <dev|holdout|all> [--examples]
Requires csd_bench.py score <tag> natural <split> and csd_bench.py oracle natural <split> first.
"""
import json, sys, csv
from pathlib import Path
import numpy as np
from csd_bench import tracks, section_notes, note_centre, WORK, crepe_cached

SR = 48000
WINDOWS = [(0, 25), (25, 50), (50, 75), (75, 100), (100, 150), (150, 200), (200, 300), (300, 10_000)]
OUT = Path(__file__).parent / "csd_onset"; OUT.mkdir(exist_ok=True)


def trace_frames(tag, stem):
    tr = json.load(open(WORK / f"{stem}_natural_{tag}.json"))
    B = tr["blockSize"]; fr = tr["frames"]
    t0 = np.array([f["timeSec"] for f in fr])
    voiced = np.array([bool(f["voiced"]) for f in fr])
    corr = np.array([f["correctionCents"] for f in fr])
    det_round = np.array([f["detectedMidi"] for f in fr])
    target = np.where(voiced, np.round(det_round + corr / 100.0), np.nan)       # target = detected + correction
    detected = np.where(voiced, target - corr / 100.0, np.nan)
    conf = np.array([f["confidence"] for f in fr])
    reason = [f.get("intentReasonName", "") for f in fr]
    return tr["reportedLatencySamples"], B, t0, voiced, target, detected, corr, conf, reason


def stats(e):
    e = np.asarray(e, float); e = e[np.isfinite(e)]
    if len(e) == 0:
        return None
    a = np.abs(e)
    return {"n": int(len(a)), "mean": round(float(a.mean()), 2), "median": round(float(np.median(a)), 2),
            "p90": round(float(np.percentile(a, 90)), 1), "p95": round(float(np.percentile(a, 95)), 1),
            "w5": round(100 * float(np.mean(a <= 5)), 1), "w10": round(100 * float(np.mean(a <= 10)), 1),
            "w20": round(100 * float(np.mean(a <= 20)), 1), "octave": int(np.sum(a >= 600))}


def analyse(tag, split, examples=False):
    win = {w: {"in": [], "out": [], "oracle": [], "wrongT": 0, "octT": 0, "rightT_off": 0, "ok": 0, "plugin_unvoiced": 0,
               "voiced_frames": 0, "frames": 0} for w in WINDOWS}
    notes_rows, cats = [], {}
    for stem in tracks(split):
        lat, B, t0, voiced, target, detected, corr, conf, reason = trace_frames(tag, stem)
        mi = crepe_cached(WORK / f"{stem}.wav")
        mo = crepe_cached(WORK / f"{stem}_natural_{tag}.wav", lat)
        mor = crepe_cached(WORK / f"{stem}_natural_oracle.wav")
        notes = section_notes(stem)
        for ni, (s, e, tg) in enumerate(notes):
            ti = np.arange(len(mi)) * 0.01
            ci = note_centre(ti, mi, s, e); co = note_centre(np.arange(len(mo)) * 0.01, mo, s, e)
            cor = note_centre(np.arange(len(mor)) * 0.01, mor, s, e)
            if not (np.isfinite(ci) and np.isfinite(co)) or abs(100 * (ci - tg)) > 48:
                continue
            ks = np.arange(int(np.ceil(s * 100)), int((e - 0.05) * 100))
            rows = []
            for k in ks:
                t = k * 0.01
                j = int(np.searchsorted(t0, t, side="right") - 1)
                if j < 0 or j >= len(t0):
                    continue
                ms = 1000 * (t - s)
                vi = mi[k] if k < len(mi) else np.nan
                vo = mo[k] if k < len(mo) else np.nan
                vr = mor[k] if k < len(mor) else np.nan
                rows.append((ms, t, vi, vo, vr, voiced[j], target[j], detected[j], corr[j], conf[j], reason[j]))
                for w in WINDOWS:
                    if w[0] <= ms < w[1]:
                        W = win[w]; W["frames"] += 1
                        W["in"].append(100 * (vi - tg)); W["out"].append(100 * (vo - tg)); W["oracle"].append(100 * (vr - tg))
                        if np.isfinite(vi):
                            W["voiced_frames"] += 1
                            if not voiced[j]:
                                W["plugin_unvoiced"] += 1
                        if voiced[j] and np.isfinite(vo):
                            if target[j] != tg:
                                W["wrongT"] += 1
                                if abs(target[j] - tg) % 12 == 0:
                                    W["octT"] += 1
                            elif abs(100 * (vo - tg)) > 10:
                                W["rightT_off"] += 1
                            else:
                                W["ok"] += 1
                        break
            # ---- note-level classification (core metric, as in csd_bench) ----
            out_err = 100 * (co - tg); in_err = 100 * (ci - tg)
            passed = abs(out_err) <= 10
            core = [r for r in rows if 80 <= r[0] and r[1] <= e - 0.05]
            vfrac = np.mean([r[5] for r in core]) if core else 0.0
            tgts = np.array([r[6] for r in core if r[5]])
            wrong_frac = float(np.mean(tgts != tg)) if len(tgts) else 1.0
            oct_frac = float(np.mean((np.abs(tgts - tg) % 12 == 0) & (tgts != tg))) if len(tgts) else 0.0
            dets = np.array([r[7] - r[2] for r in core if r[5] and np.isfinite(r[2])])
            det_bias = 100 * float(np.median(dets)) if len(dets) else np.nan
            vib = 100 * float(np.nanstd([r[2] for r in core])) if core else np.nan
            half = len(core) // 2
            second = [r[3] for r in core[half:] if np.isfinite(r[3])]
            late_ok = len(second) >= 3 and abs(100 * (np.mean(second) - tg)) <= 10
            prev = notes[ni - 1] if ni > 0 else None
            gap = (s - prev[1]) if prev else 9.9
            context = ("attack_after_rest" if gap > 0.1 else
                       "repeated_note" if prev and prev[2] == tg else
                       "semitone_step" if prev and abs(prev[2] - tg) == 1 else
                       "large_jump" if prev and abs(prev[2] - tg) >= 5 else "step_2_4")
            early_in = [r[2] for r in rows if r[0] < 60 and np.isfinite(r[2])]
            scoop = len(early_in) >= 3 and 100 * (np.mean(early_in) - ci) < -30
            if not passed:
                if np.isfinite(cor) and abs(100 * (cor - tg)) > 10:
                    c = "annotation/scorer uncertainty (oracle also fails)"
                elif oct_frac > 0.2 or abs(out_err) >= 600:
                    c = "octave error"
                elif vfrac < 0.5:
                    c = "unvoiced/voiced boundary"
                elif wrong_frac > 0.25:
                    c = ("transition ambiguity (singer >=35 c off)" if abs(in_err) >= 35 else
                         "vibrato-related wrong target" if vib >= 20 else "wrong target")
                elif np.isfinite(det_bias) and abs(det_bias) > 8:
                    c = "pitch detector error (right target)"
                elif late_ok:
                    c = "correct target / correction arrived late"
                elif np.sign(out_err) == np.sign(in_err) and abs(out_err) < abs(in_err):
                    c = "correct target / under-corrected"
                elif np.sign(out_err) != np.sign(in_err):
                    c = "correct target / over-corrected"
                else:
                    c = "other"
                cats[c] = cats.get(c, 0) + 1
            notes_rows.append({"stem": stem, "note": ni, "target": tg, "start": s, "dur": round(e - s, 3), "in_c": round(in_err, 1),
                               "out_c": round(out_err, 1), "oracle_c": round(100 * (cor - tg), 1) if np.isfinite(cor) else None,
                               "pass": passed, "wrong_target_frac": round(wrong_frac, 3), "voiced_frac": round(float(vfrac), 3),
                               "context": context, "scoop": bool(scoop), "vibrato_c": round(vib, 1) if np.isfinite(vib) else None,
                               "category": None if passed else c})
            if examples:
                (OUT / "traces").mkdir(exist_ok=True)
                with open(OUT / "traces" / f"{stem}_n{ni:03d}.csv", "w", newline="") as f:
                    wcsv = csv.writer(f)
                    wcsv.writerow(["ms_from_onset", "gt_note", "input_pitch", "selected_target", "detected_pitch", "output_pitch",
                                   "input_err_c", "output_err_c", "requested_correction_c", "confidence", "voiced", "engine_state"])
                    for (ms, t, vi, vo, vr, vc, tgt, dt, cc, cf, rs) in rows:
                        wcsv.writerow([round(ms), tg, round(vi, 3) if np.isfinite(vi) else "", tgt if vc else "",
                                       round(dt, 3) if vc else "", round(vo, 3) if np.isfinite(vo) else "",
                                       round(100 * (vi - tg), 1) if np.isfinite(vi) else "",
                                       round(100 * (vo - tg), 1) if np.isfinite(vo) else "", round(cc, 1), round(cf, 3), int(vc), rs])
    # ---- summaries ----
    table = []
    for w in WINDOWS:
        W = win[w]; judged = W["wrongT"] + W["rightT_off"] + W["ok"]
        table.append({"window_ms": f"{w[0]}-{w[1] if w[1] < 10000 else ''}", "input": stats(W["in"]), "output": stats(W["out"]),
                      "oracle": stats(W["oracle"]),
                      "A_wrong_target_pct": round(100 * W["wrongT"] / max(1, judged), 1),
                      "octave_target_pct": round(100 * W["octT"] / max(1, judged), 2),
                      "B_right_target_but_off_pct": round(100 * W["rightT_off"] / max(1, judged), 1),
                      "plugin_unvoiced_while_singing_pct": round(100 * W["plugin_unvoiced"] / max(1, W["voiced_frames"]), 1)})
    n = len(notes_rows); fails = [r for r in notes_rows if not r["pass"]]
    summary = {"tag": tag, "split": split, "notes": n, "note_pass_pct": round(100 * (n - len(fails)) / max(1, n), 2),
               "failures": len(fails), "failure_categories": {k: {"count": v, "pct_of_failures": round(100 * v / max(1, len(fails)), 1)}
                                                             for k, v in sorted(cats.items(), key=lambda kv: -kv[1])},
               "windows": table}
    json.dump(summary, open(OUT / f"onset_{tag}_{split}.json", "w"), indent=1)
    json.dump(notes_rows, open(OUT / f"notes_{tag}_{split}.json", "w"))
    return summary


if __name__ == "__main__":
    s = analyse(sys.argv[1], sys.argv[2], "--examples" in sys.argv)
    print(f"{s['tag']} {s['split']}: {s['notes']} notes, pass {s['note_pass_pct']}%")
    print(f"{'window':>9} | {'out w10':>7} {'out med':>7} {'out p95':>7} | {'orcl w10':>8} | {'A wrongT%':>9} {'B rightT-off%':>13} | {'unvoiced%':>9}")
    for r in s["windows"]:
        o, q = r["output"], r["oracle"]
        if o:
            print(f"{r['window_ms']:>9} | {o['w10']:7.1f} {o['median']:7.1f} {o['p95']:7.1f} | {q['w10'] if q else 0:8.1f} | "
                  f"{r['A_wrong_target_pct']:9.1f} {r['B_right_target_but_off_pct']:13.1f} | {r['plugin_unvoiced_while_singing_pct']:9.1f}")
    print("failure categories:", json.dumps(s["failure_categories"]))
