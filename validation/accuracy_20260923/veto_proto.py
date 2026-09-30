"""Offline prototype: AutoKey out-of-key veto (2026-09-25, Round 11).

Replays bench traces. While AutoKey supplies a key, look at the last W seconds of sung notes:
if more than TAU of that (confidence-weighted) singing lies OUTSIDE the key's scale, the key is
contradicted by what the singer is actually doing -> correct Chromatic instead until it isn't.

usage: veto_proto.py <W seconds> <TAU> [min voiced mass s]
"""
import json, sys, collections, numpy as np
from pathlib import Path
HERE = Path(__file__).parent
NAMES = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
MAJ = [0,2,4,5,7,9,11]; MIN = [0,2,3,5,7,8,10]
def pcs(r, s): return {(r + i) % 12 for i in (MAJ if s == 0 else MIN)}

def replay(fr, W, TAU, MINMASS):
    t = np.array([f['timeSec'] for f in fr]); dt = float(np.median(np.diff(t)))
    ur = np.array([f['usedRoot'] for f in fr]); us = np.array([f['usedScale'] for f in fr])
    w = np.array([f['confidence'] if (f['voiced'] and f['confidence'] > 0.5 and f['detectedMidi'] >= 0) else 0.0 for f in fr])
    pc = np.array([f['detectedMidi'] % 12 if f['detectedMidi'] >= 0 else -1 for f in fr])
    n = max(1, int(round(W / dt))); out = us.copy()
    if W <= 0: return ur, out
    # per-pitch-class cumulative weight -> window mass of any pitch class in O(1)
    cum = np.zeros((12, len(fr) + 1))
    for q in range(12): cum[q, 1:] = np.cumsum(np.where(pc == q, w, 0.0))
    for i in range(len(fr)):
        if us[i] not in (0, 1): continue
        lo = max(0, i - n + 1); byq = cum[:, i + 1] - cum[:, lo]; mass = byq.sum()
        if mass * dt < MINMASS: continue
        sc = pcs(ur[i], us[i]); off = sum(byq[q] for q in range(12) if q not in sc)
        if off / mass > TAU: out[i] = 2
    return ur, out

def csd(split, tag, W, TAU, MINMASS):
    from csd_bench import tracks, section_notes
    keys = json.load(open(HERE / 'csd_keys.json')); c = collections.Counter()
    for st in tracks(split):
        k = keys[st.split('_')[1]]; truth = (NAMES.index(k['root']), 0 if k['scale'] == 'major' else 1)
        fr = json.load(open(HERE / 'csd_work' / f'{st}_natural_{tag}.json'))['frames']
        t = np.array([f['timeSec'] for f in fr]); ur, us = replay(fr, W, TAU, MINMASS)
        for s, e, _ in section_notes(st):
            idx = np.where((t >= s + 0.08) & (t <= e - 0.05))[0]
            if len(idx) == 0: continue
            key = collections.Counter(zip(ur[idx], us[idx])).most_common(1)[0][0]
            c['chrom' if key[1] == 2 else ('right' if pcs(*key) == pcs(*truth) else 'wrong')] += 1
    n = sum(c.values()); return {k: round(100 * c[k] / n, 1) for k in ('right', 'wrong', 'chrom')}

def kc(mem, W, TAU, MINMASS):
    meta = json.load(open(HERE / 'key_change' / 'manifest.json')); acc = collections.defaultdict(list)
    for m in meta:
        fr = json.load(open(f'/tmp/kc_traces/{m["name"]}_m{mem}.json'))['frames']
        t = np.array([f['timeSec'] for f in fr]); ur, us = replay(fr, W, TAU, MINMASS); s = m['splice_sec']
        A = pcs(m['a'][0], int(m['a'][1])); Bk = pcs(m['b'][0], int(m['b'][1]))
        def share(sel, good):
            keyset = [pcs(r, q) if q in (0, 1) else None for r, q in zip(ur[sel], us[sel])]
            return (np.mean([k == good for k in keyset]), np.mean([k is not None and k != good for k in keyset]))
        acc['before_right'].append(share((t > 5) & (t < s), A)[0]); acc['before_wrong'].append(share((t > 5) & (t < s), A)[1])
        r, wr = share((t >= s) & (t < s + 20), Bk); acc['after20_right'].append(r); acc['after20_wrong'].append(wr)
    return {k: round(100 * float(np.mean(v)), 1) for k, v in acc.items()}

if __name__ == '__main__':
    W, TAU = float(sys.argv[1]), float(sys.argv[2]); MINMASS = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
    for tag, mem in (('akfix_chrom', 15), ('akmem30', 30)):
        print(f'mem {mem:2d} W {W} tau {TAU}: CSD dev {csd("dev", tag, W, TAU, MINMASS)} | holdout {csd("holdout", tag, W, TAU, MINMASS)}')
        print(f'          key change {kc(mem, W, TAU, MINMASS)}')
