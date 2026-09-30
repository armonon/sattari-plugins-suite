"""AutoKey through REAL-vocal key changes (modulations), replacing reliance on the synthetic key_change.py.

make: each test clip splices real lead vocals (al_manifest.json, Apple Loops; see al_vocals.py) of one
singer SEX: section A = songs labelled key A (>= 25 s, same pack preferred), then section B = other
songs labelled key B (>= 35 s). Sections longer than the cap are cut at the quietest point between
min and cap; songs are joined with 0.3 s gaps and levelled to the same active RMS. No pitch shifting.
Change types: half / whole / fourth (up, same mode), relative (maj <-> rel. min), parallel
(maj <-> min, same root), none (control: key A, then different songs still in key A).
Split: a clip uses only dev songs or only holdout songs. Plus CSD clips (Choral Singing Dataset,
one choir singer's piece followed by another singer's piece in a different key), kind 'csd'.

run: renders traces only (AutoPitchBench --trace, AutoKey on, manual key untouched, block 512,
Classic, 8 in parallel), scores on the NOTE SET (relative keys snap identically, so 'relative'
and 'none' are 'no set change'), deletes the traces.
  delay     = s after the splice until the USED key is key B's set and stays there to the end
  fail-safe = s after the splice until the used key is key A's set for the last time
  wrong     = voiced time the used key is a set that is neither the current section's nor chromatic,
              before the splice / 0-10 s / 10-30 s / 30+ s after it
  lenient   = s until the used key is first B's set with no wrong set afterwards (chromatic allowed)
  raw%      = raw detector verdict (before the gate) = section's set, voiced; a check on Apple's labels
  false tr. = used-key changes INTO a wrong set; chatter = used-key set changes (chromatic counts as
              a state) per minute; coverage = voiced time with the right set.

usage: key_change_real.py make | run <tag> [bench args]      BENCH=<AutoPitchBench>
"""
import collections, json, os, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np, soundfile as sf
from diskguard import require_free

HERE = Path(__file__).parent; OUT = HERE / 'key_change_real'; SR = 48000
B = Path(os.environ.get('BENCH', str(HERE.parents[1] / 'build/auto-pitch-neural/AutoPitchBench')))
N = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B']
MAJ = [0, 2, 4, 5, 7, 9, 11]; MIN = [0, 2, 3, 5, 7, 8, 10]
A_MIN, A_CAP, B_MIN, B_CAP, GAP = 25.0, 40.0, 35.0, 55.0, 0.3
TYPES = {'half': lambda r, mi: ((r + 1) % 12, mi), 'whole': lambda r, mi: ((r + 2) % 12, mi),
         'fourth': lambda r, mi: ((r + 5) % 12, mi),
         'relative': lambda r, mi: ((r + 3) % 12, False) if mi else ((r + 9) % 12, True),
         'parallel': lambda r, mi: (r, not mi), 'none': lambda r, mi: (r, mi)}
PER_CELL = {'dev': 5, 'holdout': 2}
# holdout male has no material for half/whole/fourth: give its budget to the cells that exist
HOLDOUT_MALE_EXTRA = {'relative': 3, 'parallel': 3, 'none': 2}
CSD = [('dev', 'LI', 'soprano_1', 'ND', 'soprano_2'), ('dev', 'ER', 'soprano_2', 'LI', 'soprano_3'),
       ('dev', 'LI', 'tenor_1', 'ER', 'tenor_2'), ('dev', 'ND', 'soprano_3', 'ER', 'soprano_1'),
       ('holdout', 'LI', 'soprano_4', 'ND', 'soprano_4'), ('holdout', 'LI', 'tenor_4', 'ER', 'tenor_4')]
WINDOWS = [('pre', -1e9, 0), ('0-10', 0, 10), ('10-30', 10, 30), ('30+', 30, 1e9)]


def pcs(r, minor): return frozenset((r + i) % 12 for i in (MIN if minor else MAJ))
def kname(k): return N[k[0]] + ('m' if k[1] else '')


def level(x):
    """Active RMS to -20 dBFS (frames within 30 dB of the loudest), peak <= 0.98."""
    fr = x[:len(x) // 480 * 480].reshape(-1, 480); e = np.sqrt((fr ** 2).mean(1) + 1e-12)
    act = e[e > e.max() * 10 ** (-30 / 20)]; g = 0.1 / np.sqrt((act ** 2).mean())
    return x * min(g, 0.98 / (np.abs(x).max() + 1e-9))


def cut(x, lo, cap):
    """Cut x at the quietest 50 ms point between lo and cap seconds (if longer than cap)."""
    if len(x) <= cap * SR: return x
    hop = 480; s0, s1 = int(lo * SR), int(cap * SR) - 2400
    e = [np.sqrt((x[i:i + 2400] ** 2).mean()) for i in range(s0, s1, hop)]
    c = s0 + int(np.argmin(e)) * hop + 1200; y = x[:c].copy(); f = min(960, c)
    y[-f:] *= np.linspace(1, 0, f); return y


def section(pool, lo, cap, rng, avoid_pack=None):
    """Songs from pool (same pack as the first preferred) until >= lo s; None if not enough."""
    if not pool: return None
    order = list(rng.permutation(len(pool)))
    if avoid_pack:
        order.sort(key=lambda i: pool[i]['pack'] == avoid_pack)      # stable: other packs first
    first = pool[order[0]]; rest = [pool[i] for i in order[1:]]
    rest.sort(key=lambda m: m['pack'] != first['pack'])
    chosen, dur = [], 0.0
    for m in [first] + rest:
        chosen.append(m); dur += m['dur'] + GAP
        if dur >= lo: break
    if dur < lo: return None
    parts, songs, t = [], [], 0.0
    for m in chosen:
        x, _ = sf.read(m['wav']); x = level(x)
        songs.append({'name': m['name'], 'start': round(t, 3)}); parts += [x, np.zeros(int(GAP * SR))]; t += len(x) / SR + GAP
    y = cut(np.concatenate(parts[:-1]), lo, cap)
    songs = [s for s in songs if s['start'] < len(y) / SR]
    return y, songs


def make():
    require_free()
    OUT.mkdir(exist_ok=True)
    for f in OUT.glob('*.wav'): f.unlink()
    rng = np.random.default_rng(20260925)
    items = json.load(open(HERE / 'al_manifest.json'))
    for m in items: m['dur'] = sf.info(m['wav']).duration
    by = collections.defaultdict(list)
    for m in items: by[(m['split'], m['sex'], m['root'], m['minor'])].append(m)
    tot = lambda sp, sx, k: sum(m['dur'] + GAP for m in by[(sp, sx, *k)])
    meta = []

    def emit(name, split, sex, kind, ka, kb, ya, sa, yb, sb, source):
        y = np.concatenate([ya, np.zeros(int(GAP * SR)), yb])
        sf.write(OUT / f'{name}.wav', y, SR, subtype='PCM_16')
        sp = (len(ya) / SR) + GAP
        meta.append({'name': name, 'source': source, 'split': split, 'sex': sex, 'kind': kind,
                     'a': [ka[0], bool(ka[1])], 'b': [kb[0], bool(kb[1])], 'a_name': kname(ka), 'b_name': kname(kb),
                     'splice_sec': round(sp, 4), 'dur': round(len(y) / SR, 3),
                     'a_songs': sa, 'b_songs': [{'name': s['name'], 'start': round(s['start'] + sp, 3)} for s in sb]})

    for split in ('dev', 'holdout'):
        for sex in ('Male', 'Female'):
            for kind, f in TYPES.items():
                keys = [(r, mi) for r in range(12) for mi in (False, True)]
                if kind == 'none':
                    pairs = [(k, k) for k in keys if tot(split, sex, k) >= A_MIN + B_MIN and len(by[(split, sex, *k)]) >= 2]
                else:
                    pairs = [(k, f(*k)) for k in keys if tot(split, sex, k) >= A_MIN and tot(split, sex, f(*k)) >= B_MIN]
                want = PER_CELL[split]
                if split == 'holdout' and sex == 'Male': want = HOLDOUT_MALE_EXTRA.get(kind, want)
                pairs = [pairs[i] for i in rng.permutation(len(pairs))]
                made, tries = 0, 0
                while pairs and made < want and tries < 50:
                    ka, kb = pairs[tries % len(pairs)]; tries += 1
                    A = section(by[(split, sex, *ka)], A_MIN, A_CAP, rng)
                    if A is None: continue
                    used = {s['name'] for s in A[1]}
                    poolb = [m for m in by[(split, sex, *kb)] if m['name'] not in used]
                    pack_a = next(m['pack'] for m in items if m['name'] == A[1][0]['name'])
                    Bs = section(poolb, B_MIN, B_CAP, rng, avoid_pack=pack_a if kind == 'none' else None)
                    if Bs is None: continue
                    emit(f'{split}_{kind}_{sex[0]}_{kname(ka)}-{kname(kb)}_{made}', split, sex, kind, ka, kb, *A, *Bs, 'al')
                    made += 1
                if made < want: print(f'note: {split} {sex} {kind}: made {made}/{want}')
    ck = json.load(open(HERE / 'csd_keys.json'))
    for split, pa, sa, pb, sb in CSD:
        ka, kb = [(N.index(ck[p]['root']), ck[p]['scale'] == 'minor') for p in (pa, pb)]
        rel = next((k for k, f in TYPES.items() if f(*ka) == kb), 'other')
        ya = cut(level(sf.read(HERE / f'csd_work/CSD_{pa}_{sa}.wav')[0]), A_MIN, A_CAP)
        yb = cut(level(sf.read(HERE / f'csd_work/CSD_{pb}_{sb}.wav')[0]), B_MIN, B_CAP)
        sex = 'Female' if sa.split('_')[0] in ('soprano', 'alto') else 'Male'
        emit(f'{split}_csd_{pa}_{sa}-{pb}_{sb}', split, sex, 'csd', ka, kb, ya, [{'name': f'CSD_{pa}_{sa}', 'start': 0.0}],
             yb, [{'name': f'CSD_{pb}_{sb}', 'start': 0.0}], f'csd:{rel}')
    json.dump(meta, open(OUT / 'manifest.json', 'w'), indent=1)
    c = collections.Counter((m['split'], m['kind'], m['sex'][0]) for m in meta)
    print('made', len(meta), dict(sorted(c.items())))
    print('size', round(sum(f.stat().st_size for f in OUT.glob('*.wav')) / 1e6), 'MB')


def score(m, fr):
    t = np.array([f['timeSec'] for f in fr]); dt = float(np.median(np.diff(t))); s = m['splice_sec']
    v = np.array([bool(f['voiced']) for f in fr])
    A, Bset = pcs(m['a'][0], m['a'][1]), pcs(m['b'][0], m['b'][1])
    used = [pcs(f['usedRoot'] % 12, f['usedScale'] == 1) if f['usedScale'] in (0, 1) else 'chrom' for f in fr]
    truth = [A if ti < s else Bset for ti in t]
    right = np.array([u == tr for u, tr in zip(used, truth)])
    wrong = np.array([u != 'chrom' and u != tr for u, tr in zip(used, truth)])
    old = np.array([u == A and ti >= s and A != Bset for u, ti in zip(used, t)])
    post = t >= s
    r = {'name': m['name'], 'split': m['split'], 'kind': m['kind'], 'sex': m['sex'], 'source': m['source'],
         'set_change': A != Bset, 'voiced_sec': float(v.sum() * dt)}

    def last_idx(ok):            # first post-splice index from which ok holds to the end
        bad = np.where(~ok & post)[0]
        i = (bad[-1] + 1) if len(bad) else int(np.argmax(post))
        return max(i, int(np.argmax(post)))
    if A != Bset:
        isb = np.array([u == Bset for u in used]); i = last_idx(isb)
        r['delay'] = float(t[i] - s) if i < len(t) else None
        nota = np.array([u != A for u in used]); i = last_idx(nota)
        r['failsafe'] = float(max(0.0, t[i] - s)) if i < len(t) else None
        r['old_key_sec_post'] = float((old & v).sum() * dt)
        # lenient: first time the used key is B's set, never a wrong set afterwards (chromatic allowed)
        okl = np.array([u == Bset or u == 'chrom' for u in used]); i = last_idx(okl)
        j = next((k for k in range(i, len(t)) if isb[k]), None)
        r['delay_lenient'] = float(t[j] - s) if j is not None else None
    for w, lo, hi in WINDOWS:
        sel = (t - s >= lo) & (t - s < hi) & v
        r[f'wrong_{w}'] = float((wrong & sel).sum() * dt); r[f'voiced_{w}'] = float(sel.sum() * dt)
        r[f'right_{w}'] = float((right & sel).sum() * dt)
    ch = [i for i in range(1, len(used)) if used[i] != used[i - 1]]
    into_wrong = [i for i in ch if wrong[i]]
    r['false_tr_pre'] = sum(t[i] < s for i in into_wrong)
    r['false_tr_post'] = sum(t[i] >= s for i in into_wrong)
    # "steady" = before the splice or >= 30 s after it (away from the change itself)
    r['false_tr_steady'] = sum(t[i] < s or t[i] >= s + 30 for i in into_wrong)
    r['changes'] = len(ch); r['chatter_per_min'] = 60 * len(ch) / (t[-1] + dt)
    # label sanity: raw detector verdict (no gate) equals the section's set, voiced frames
    raw = [pcs(f['detectedRoot'] % 12, f['detectedScale'] == 1) if f['detectedScale'] in (0, 1) else None for f in fr]
    rawok = np.array([x == tr for x, tr in zip(raw, truth)])
    r['raw_pre'] = float((rawok & v & ~post).sum() / max(1, (v & ~post).sum()))
    late = v & (t >= s + 10); r['raw_post10'] = float((rawok & late).sum() / max(1, late.sum()))
    # label check independent of the key detector: share of voiced sung notes (detectedMidi) inside
    # the labelled set vs inside the best-fitting diatonic set, per section
    sets = [pcs(k, False) for k in range(12)]
    for sec, sel, lab in (('a', ~post, A), ('b', post, Bset)):
        h = np.bincount([f['detectedMidi'] % 12 for f, ok in zip(fr, sel & v) if ok and f['detectedMidi'] >= 0], minlength=12)
        tot_ = max(1, h.sum()); r[f'inset_{sec}'] = float(sum(h[list(lab)]) / tot_)
        r[f'bestset_{sec}'] = float(max(sum(h[list(x)]) for x in sets) / tot_)
    r['coverage'] = float((right & v).sum() / max(1, v.sum()))
    r['coverage_pre'] = float((right & v & ~post).sum() / max(1, (v & ~post).sum()))
    r['coverage_post'] = float((right & v & post).sum() / max(1, (v & post).sum()))
    return r


def summarize(rs):
    def pooled(k, w): vs = sum(x[f'voiced_{w}'] for x in rs); return 100 * sum(x[f'{k}_{w}'] for x in rs) / max(1e-9, vs)
    o = {'n': len(rs)}
    if rs and all(x['set_change'] for x in rs):
        d = [x['delay'] for x in rs if x['delay'] is not None]; fs = [x['failsafe'] for x in rs if x['failsafe'] is not None]
        dl = [x['delay_lenient'] for x in rs if x['delay_lenient'] is not None]
        o.update(followed_lenient=len(dl), delay_lenient_med=float(np.median(dl)) if dl else None)
        o.update(followed=len(d), delay_med=float(np.median(d)) if d else None, delay_p90=float(np.percentile(d, 90)) if d else None,
                 failsafe_med=float(np.median(fs)) if fs else None, failsafe_p90=float(np.percentile(fs, 90)) if fs else None,
                 old_key_sec_post=float(np.mean([x['old_key_sec_post'] for x in rs])))
    for w, _, _ in WINDOWS:
        o[f'wrong_{w}_pct'] = pooled('wrong', w); o[f'wrong_{w}_sec'] = float(np.mean([x[f'wrong_{w}'] for x in rs]))
    o['wrong_all_pct'] = 100 * sum(sum(x[f'wrong_{w}'] for w, _, _ in WINDOWS) for x in rs) / max(1e-9, sum(x['voiced_sec'] for x in rs))
    for k in ('inset_a', 'bestset_a', 'inset_b', 'bestset_b', 'raw_pre', 'raw_post10', 'false_tr_pre', 'false_tr_post', 'false_tr_steady', 'chatter_per_min', 'coverage', 'coverage_pre', 'coverage_post'):
        o[k] = float(np.mean([x[k] for x in rs]))
    o['clips_with_false_tr'] = sum((x['false_tr_pre'] + x['false_tr_post']) > 0 for x in rs)
    return o


def run(tag, extra):
    meta = json.load(open(OUT / 'manifest.json'))
    args = ['--auto-key', '--manual-key-untouched', '--block', '512', '--style', 'Classic', *extra]

    def one(m):
        tr = f"/tmp/kcr_{tag}_{m['name']}_{os.getpid()}.json"
        env = dict(os.environ, SATTARI_SESSION_ID=f"kcr-{tag}-{m['name']}-{os.getpid()}")
        subprocess.run([str(B), str(OUT / f"{m['name']}.wav"), '--trace', tr, *args], capture_output=True, env=env, check=True)
        try: return score(m, json.load(open(tr))['frames'])
        finally: os.unlink(tr)
    with ThreadPoolExecutor(8) as ex: rs = list(ex.map(one, meta))
    kinds = ['half', 'whole', 'fourth', 'parallel', 'relative', 'none', 'csd']
    summ = {}
    for split in ('dev', 'holdout'):
        sub = [x for x in rs if x['split'] == split]
        groups = {k: [x for x in sub if x['kind'] == k] for k in kinds}
        groups['set change (AL)'] = [x for x in sub if x['source'] == 'al' and x['set_change']]
        groups['no set change (AL)'] = [x for x in sub if x['source'] == 'al' and not x['set_change']]
        summ[split] = {k: summarize(g) for k, g in groups.items() if g}
    json.dump({'tag': tag, 'args': args, 'summary': summ, 'clips': rs}, open(OUT / f'result_{tag}.json', 'w'), indent=1,
              default=lambda o: o.item())
    f1 = lambda v: '  -  ' if v is None else f'{v:5.1f}'
    for split, groups in summ.items():
        print(f'{tag} {split}: kind n | strict: followed, delay med/p90 s | lenient: followed, med s | fail-safe med s | '
              f'wrong% pre/0-10/10-30/30+ | false tr/clip pre/post | chatter/min | coverage% pre/post | raw% pre/post10')
        for k, o in groups.items():
            fol = (f"{o['followed']:2d}/{o['n']:<2d} {f1(o['delay_med'])}/{f1(o['delay_p90'])} | {o['followed_lenient']:2d} {f1(o['delay_lenient_med'])} | "
                   f"{f1(o['failsafe_med'])}") if 'followed' in o else f"no set change      |    -     |   -  "
            print(f"  {k:18s} {o['n']:2d} | {fol} | " + '/'.join(f"{o[f'wrong_{w}_pct']:4.1f}" for w, _, _ in WINDOWS)
                  + f" | {o['false_tr_pre']:.2f}/{o['false_tr_post']:.2f} | {o['chatter_per_min']:4.1f} | "
                  f"{100 * o['coverage_pre']:4.1f}/{100 * o['coverage_post']:4.1f} | {100 * o['raw_pre']:4.1f}/{100 * o['raw_post10']:4.1f}")


if __name__ == '__main__':
    make() if sys.argv[1] == 'make' else run(sys.argv[2], sys.argv[3:])
