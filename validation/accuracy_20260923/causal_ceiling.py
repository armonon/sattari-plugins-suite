"""How much of the gap is reachable by a realtime (causal) tuner?
Analytic: assume output pitch == chosen target exactly; score each note's core mean vs ground truth.
Strategies are fed the SINGER's reference pitch (CREPE-tiny), i.e. a perfect detector."""
import json, sys, numpy as np
from csd_bench import tracks, section_notes, note_centre, WORK, crepe_cached
from csd_onset import trace_frames
split = sys.argv[1] if len(sys.argv) > 1 else "dev"

def hyst_targets(m, margin=0.5, hold=0.12, hop=0.01):
    out = np.full(len(m), np.nan); held = -1; away = 0.0; silent = 0.0
    for i, v in enumerate(m):
        if not np.isfinite(v):
            silent += hop
            if silent > 0.15: held = -1
            continue
        silent = 0.0; near = int(round(v))
        if held < 0 or near == held: held = near; away = 0.0
        else:
            away += hop
            if abs(v - held) - abs(v - near) > margin or away >= hold: held = near; away = 0.0
        out[i] = held
    return out

def ema_targets(m, tau, hop=0.01):
    out = np.full(len(m), np.nan); e = None; away = 0.0; held = -1; silent = 1.0
    for i, v in enumerate(m):
        if not np.isfinite(v): silent += hop; continue
        if e is None or silent > 0.04: e = v; held = -1
        silent = 0.0
        if abs(v - e) > 0.75:
            away += hop
            if away >= 0.04: e = v; away = 0.0; held = -1
            else: out[i] = round(v); continue
        else:
            away = 0.0; e += (1 - np.exp(-hop / tau)) * (v - e)
        c = int(round(e))
        if held < 0 or (c != held and abs(e - held) - abs(e - c) > 0.2): held = c
        out[i] = held
    return out

strategies = {"follow singer (nearest note, no hold)": lambda m: np.where(np.isfinite(m), np.round(m), np.nan),
              "follow singer + production hold (0.5 st / 120 ms)": lambda m: hyst_targets(m),
              "follow singer + 100 ms average (E1 logic)": lambda m: ema_targets(m, 0.10)}
res = {k: [0, 0] for k in list(strategies) + ["R0 chosen targets (from trace)"]}
for stem in tracks(split):
    mi = crepe_cached(WORK / f"{stem}.wav"); ti = np.arange(len(mi)) * 0.01
    tg_series = {k: f(mi) for k, f in strategies.items()}
    lat, B, t0, voiced, target, detected, corr, conf, reason = trace_frames("r0", stem)
    j = np.clip(np.searchsorted(t0, ti, side="right") - 1, 0, len(t0) - 1)
    tg_series["R0 chosen targets (from trace)"] = np.where(voiced[j], target[j], np.nan)
    for s, e, tg in section_notes(stem):
        ci = note_centre(ti, mi, s, e)
        if not np.isfinite(ci) or abs(100 * (ci - tg)) > 48: continue
        for k, ser in tg_series.items():
            c = note_centre(ti, ser.astype(float), s, e)
            if np.isfinite(c):
                res[k][0] += 1; res[k][1] += abs(100 * (c - tg)) <= 10
for k, (n, ok) in res.items():
    print(f"{k:52} {100 * ok / n:5.1f}%  (n={n})")
