"""Why is the target wrong at onset, and why is the right target under-delivered?"""
import json, sys, numpy as np
from csd_bench import tracks, section_notes, note_centre, WORK, crepe_cached
from csd_onset import trace_frames, WINDOWS
tag, split = sys.argv[1], sys.argv[2]
agg = {w: {"judged": 0, "in_on_gt": 0, "wrong": 0, "wrong_is_input_nearest": 0, "wrong_is_prev_note": 0, "wrong_other": 0,
           "right": 0, "remain": [], "req_vs_need": []} for w in WINDOWS}
for stem in tracks(split):
    lat, B, t0, voiced, target, detected, corr, conf, reason = trace_frames(tag, stem)
    mi = crepe_cached(WORK / f"{stem}.wav"); mo = crepe_cached(WORK / f"{stem}_natural_{tag}.wav", lat)
    notes = section_notes(stem)
    for ni, (s, e, tg) in enumerate(notes):
        ci = note_centre(np.arange(len(mi)) * 0.01, mi, s, e)
        if not np.isfinite(ci) or abs(100 * (ci - tg)) > 48: continue
        prev = notes[ni - 1][2] if ni > 0 else None
        for k in range(int(np.ceil(s * 100)), int((e - 0.05) * 100)):
            t = k * 0.01; j = int(np.searchsorted(t0, t, side="right") - 1)
            if j < 0 or k >= len(mi) or k >= len(mo) or not voiced[j] or not np.isfinite(mi[k]) or not np.isfinite(mo[k]): continue
            ms = 1000 * (t - s); w = next(w for w in WINDOWS if w[0] <= ms < w[1]); A = agg[w]
            A["judged"] += 1; nearest = round(mi[k]); A["in_on_gt"] += nearest == tg
            if target[j] != tg:
                A["wrong"] += 1
                if target[j] == nearest: A["wrong_is_input_nearest"] += 1
                elif prev is not None and target[j] == prev: A["wrong_is_prev_note"] += 1
                else: A["wrong_other"] += 1
            else:
                A["right"] += 1; need = 100 * (tg - mi[k])
                if abs(need) > 10:
                    A["remain"].append((mo[k] - tg) / (mi[k] - tg))
                    A["req_vs_need"].append(corr[j] - need)
print(f"{'window':>9} | {'singer on gt note':>17} | {'wrong: =singer nearest':>22} {'=previous note':>14} {'other':>6} | {'right target: error remaining':>29} {'req-need med':>12}")
for w in WINDOWS:
    A = agg[w]; wr = max(1, A["wrong"])
    rem = np.median(A["remain"]) if A["remain"] else float("nan"); rq = np.median(A["req_vs_need"]) if A["req_vs_need"] else float("nan")
    print(f"{str(w[0])+'-'+(str(w[1]) if w[1]<10000 else ''):>9} | {100*A['in_on_gt']/max(1,A['judged']):16.1f}% | {100*A['wrong_is_input_nearest']/wr:21.1f}% {100*A['wrong_is_prev_note']/wr:13.1f}% {100*A['wrong_other']/wr:5.1f}% | {100*rem:28.0f}% {rq:+11.1f}c")
