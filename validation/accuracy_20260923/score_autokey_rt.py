"""Auto Pitch AutoKey at realtime pace (as in a DAW), 12 clips in parallel.

usage: score_autokey_rt.py [tag] [--corpus synth_key|csd] [--split dev|holdout|all] [--gate G] [bench args...]
  BENCH=<path to AutoPitchBench> (default: <repo>/build/auto-pitch-neural/AutoPitchBench)

Scores two things per clip:
  raw   = the detector's verdict (UiFrame.detectedRoot/Scale), the old 88.5% number
  used  = the key correction actually uses (KeyOrigin: below the confidence gate AutoKey
          falls back to the manual key, here the plugin default A minor)
Over time: share of the clip where the used key is right, wrong (a confident wrong verdict),
or the fallback (no confident verdict). `used` is also re-derived offline for other gates
(--sweep) from the per-frame margin, so a gate can be calibrated without re-rendering.
"""
import json, os, subprocess, collections, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np
from score_key import parse, rel, OUT, NAMES
HERE = Path(__file__).parent
B = Path(os.environ.get('BENCH', str(HERE.parents[1] / 'build/auto-pitch-neural/AutoPitchBench')))
MANUAL = (9, True)            # plugin default: A minor


def opt(name, default):
    if name in sys.argv:
        i = sys.argv.index(name); v = sys.argv[i + 1]; del sys.argv[i:i + 2]; return v
    return default


corpus = opt('--corpus', 'synth_key'); split = opt('--split', 'all'); gate = float(opt('--gate', '0.10'))
sweep = '--sweep' in sys.argv
if sweep: sys.argv.remove('--sweep')
tag = sys.argv[1] if len(sys.argv) > 1 else 'rt'
extra = sys.argv[2:]
WORKDIR = Path(f'/tmp/akrt_{os.getpid()}'); WORKDIR.mkdir(exist_ok=True)

if corpus == 'synth_key':
    items = [{'file': m['file'], 'wav': str(OUT / 'wav' / m['file']), 'label': m['label'], 'kind': m['kind']}
             for m in json.load(open(OUT / 'manifest.json'))]
else:
    keys = json.load(open(HERE / 'csd_keys.json')); sp = json.load(open(HERE / 'csd_split.json'))
    stems = sp['dev_tracks'] if split == 'dev' else sp['holdout_tracks'] if split == 'holdout' else sp['dev_tracks'] + sp['holdout_tracks']
    items = []
    for st in stems:
        k = keys[st.split('_')[1]]
        items.append({'file': st, 'wav': str(HERE / 'csd_work' / f'{st}.wav'), 'label': f"{k['root']} {k['scale']}",
                      'kind': st.split('_')[2]})


def run(m):
    tr = WORKDIR / f"{m['file']}.json"
    env = dict(os.environ, SATTARI_SESSION_ID=f"akrt-{tag}-{m['file']}-{os.getpid()}")
    subprocess.run([str(B), m['wav'], '--output', str(WORKDIR / f"{m['file']}.out.wav"), '--trace', str(tr), '--auto-key',
                    '--key', 'A', '--scale', 'minor', '--block', '512', '--realtime', *extra], capture_output=True, env=env)
    fr = json.load(open(tr))['frames']; tr.unlink(); (WORKDIR / f"{m['file']}.out.wav").unlink(missing_ok=True)
    t = np.array([f['timeSec'] for f in fr])
    raw = [(f['detectedRoot'] % 12, f.get('detectedScale', 0) == 1) for f in fr]
    used = [(f['usedRoot'] % 12, f.get('usedScale', 0) == 1) for f in fr] if 'usedRoot' in fr[-1] else None
    conf = np.array([f.get('keyConf', np.nan) for f in fr])
    return m, t, raw, used, conf


label_of = {m['file']: parse(m['label']) for m in items}
cats = collections.Counter(); ucats = collections.Counter(); bykind = collections.defaultdict(list); res = []
series = []   # (conf, raw_correct, clip_weight) per frame, for the gate sweep
tot = collections.Counter()
with ThreadPoolExecutor(12) as ex:
    for m, t, raw, used, conf in ex.map(run, items):
        lab = label_of[m['file']]
        c = rel(raw[-1], lab); cats[c] += 1; bykind[m['kind']].append(c == 'exact')
        rc = np.array([r == lab for r in raw])
        gated = conf >= gate
        # Share of the clip (block-weighted, i.e. time) in each state of the USED key.
        right = float(np.mean(gated & rc)); wrong = float(np.mean(gated & ~rc)); fb = float(np.mean(~gated))
        tot['right'] += right; tot['wrong'] += wrong; tot['fallback'] += fb; tot['raw_right'] += float(np.mean(rc)); tot['n'] += 1
        uc = rel(used[-1], lab) if used else 'n/a'; ucats[uc] += 1
        # Consistency check: the offline gate must reproduce the plugin's own used key.
        if used is not None and abs(gate - 0.10) < 1e-9:
            derived = [r if g else MANUAL for r, g in zip(raw, gated)]
            tot['mismatch'] += sum(d != u for d, u in zip(derived, used))
        # First time the used key is right and stays right to the end.
        good = gated & rc; settle = next((t[i] for i in range(len(t)) if good[i:].all()), None)
        series.append((conf[::5], rc[::5], len(rc[::5])))
        res.append({**m, 'pred_raw': f"{NAMES[raw[-1][0]]} {'minor' if raw[-1][1] else 'major'}", 'cat_raw': c,
                    'cat_used_final': uc, 'final_conf': float(conf[-1]), 'time_right': right, 'time_wrong': wrong,
                    'time_fallback': fb, 'settle_sec': settle})
out = (OUT if corpus == 'synth_key' else HERE / 'csd_work') / f'score_autopitch_autokey_{corpus}_{tag}.json'
json.dump(res, open(out, 'w'), indent=1)
n = len(res)
print(f'Auto Pitch AutoKey (realtime, {corpus}/{split}, {tag}): RAW final', cats['exact'], '/', n,
      f"= {100 * cats['exact'] / n:.1f}% exact |", dict(cats))
for k, v in bykind.items(): print(f'   {k}: {sum(v)}/{len(v)}')
print(f"   USED final exact {ucats['exact']}/{n} {dict(ucats)}")
print(f"   USED over time (gate {gate}): right {100 * tot['right'] / n:.1f}% | wrong {100 * tot['wrong'] / n:.1f}% | "
      f"fallback {100 * tot['fallback'] / n:.1f}% | raw right {100 * tot['raw_right'] / n:.1f}%"
      + (f" | offline/plugin mismatch frames {tot['mismatch']}" if 'mismatch' in tot else ''))
st = [r['settle_sec'] for r in res if r['settle_sec'] is not None]
print(f"   settled (used right to the end): {len(st)}/{n}, median {np.median(st) if st else float('nan'):.1f} s")
if sweep:
    C = np.concatenate([s[0] for s in series]); R = np.concatenate([s[1] for s in series])
    W = np.concatenate([np.full(s[2], 1.0 / s[2]) for s in series]) / len(series)   # every clip weighs the same
    ok = np.isfinite(C)
    print('   margin of RIGHT verdicts: p5/p25/p50 %.3f/%.3f/%.3f' % tuple(np.percentile(C[ok & R], [5, 25, 50])))
    if (ok & ~R).any():
        print('   margin of WRONG verdicts: p50/p75/p95 %.3f/%.3f/%.3f' % tuple(np.percentile(C[ok & ~R], [50, 75, 95])))
    for g in (0.0, 0.02, 0.04, 0.05, 0.06, 0.08, 0.10, 0.12, 0.15):
        gg = C >= g
        print(f'   gate {g:.2f}: used right {100 * W[gg & R].sum():5.1f}% | wrong {100 * W[gg & ~R].sum():5.1f}% | fallback {100 * W[~gg].sum():5.1f}%')
