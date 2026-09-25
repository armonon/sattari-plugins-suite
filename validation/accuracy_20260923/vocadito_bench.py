"""Auto Pitch on vocadito (Bittner et al. 2021, CC BY 4.0, zenodo.org/records/5578807):
40 real solo singing excerpts, 7 languages, singers of mixed training, 2 independent note annotations.

Scoring as csd_bench.py: a note passes when the mean of its core (onset + 80 ms .. end - 50 ms) in
the output lands within 10 c of its target. Target = the nearest semitone of the annotated note
pitch; only notes where BOTH annotators round to the same semitone are used. Split BY SINGER:
holdout = every 3rd singer id in sorted order. Key for keyed mode = duration-weighted Temperley
fit of annotator 1's notes (an oracle; not what AutoKey sees). Renders are deleted after scoring.

usage: vocadito_bench.py score <tag> <dev|holdout|all> <chromatic|keyed|autokey> [bench args]
"""
import csv, json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np
from pitchref import load, f0, SR
HERE = Path(__file__).parent; DS = Path.home() / 'datasets/vocadito'; WORK = HERE / 'vocadito_work'; WORK.mkdir(exist_ok=True)
B = Path(os.environ.get('BENCH', str(HERE.parents[1] / 'build/auto-pitch-neural/AutoPitchBench')))
NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
TEMP_MAJ = [5, 2, 3.5, 2, 4.5, 4, 2, 4.5, 2, 3.5, 1.5, 4]; TEMP_MIN = [5, 2, 3.5, 4.5, 2, 4, 2, 4.5, 3.5, 2, 1.5, 4]
midi = lambda hz: 69 + 12 * np.log2(np.asarray(hz, float) / 440.0)
meta = {r['track_id']: r for r in csv.DictReader(open(DS / 'vocadito_metadata.csv'))}
singers = sorted({r['singer_id'] for r in meta.values()}, key=lambda s: int(s[1:]))
HOLD = set(singers[2::3])

def tracks(split):
    return [t for t in sorted(meta, key=int) if split == 'all' or (meta[t]['singer_id'] in HOLD) == (split == 'holdout')]

def notes(t, a):
    return [(float(s), float(s) + float(d), float(midi(float(f)))) for s, f, d in csv.reader(open(DS / f'Annotations/Notes/vocadito_{t}_notes{a}.csv'))]

def agreed(t):
    a2 = notes(t, 'A2'); out = []
    for s, e, m in notes(t, 'A1'):
        mid = (s + e) / 2; o = [n for n in a2 if n[0] <= mid <= n[1]]
        if o and round(o[0][2]) == round(m): out.append((s, e, int(round(m))))
    return out

def key_of(t):
    h = np.zeros(12)
    for s, e, m in notes(t, 'A1'): h[int(round(m)) % 12] += e - s
    best = max(((np.corrcoef(h, np.roll(p, r))[0, 1], r, mi) for r in range(12) for mi, p in ((0, TEMP_MAJ), (1, TEMP_MIN))))
    return NAMES[best[1]], 'minor' if best[2] else 'major'

def centre(tt, m, s, e):
    sel = (tt >= s + 0.08) & (tt <= e - 0.05) & np.isfinite(m)
    return float(np.mean(m[sel])) if sel.sum() >= 5 else float('nan')

def ref(path, lat=0):
    c = Path(str(path) + f'.lat{lat}.crepetiny.s0.npy')
    if c.exists(): return np.load(c)
    m, _ = f0(load(path)[lat:], model='tiny'); np.save(c, m); return m

def score(tag, split, mode, extra):
    def run(t):
        src = WORK / f'v{t}.wav'
        if not src.exists():
            subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', str(DS / f'Audio/vocadito_{t}.wav'), '-ac', '1', '-ar', str(SR), str(src)], check=True)
        out = WORK / f'v{t}_{tag}.wav'; tr = WORK / f'v{t}_{tag}.json'
        k, sc = key_of(t) if mode == 'keyed' else ('C', 'chromatic')
        args = ['--auto-key', '--manual-key-untouched'] if mode == 'autokey' else ['--key', k, '--scale', sc]
        subprocess.run([str(B), str(src), '--output', str(out), '--trace', str(tr), '--amount', '1', '--speed', '1', '--block', '512',
                        *args, *(extra or ['--style', 'Classic'])], check=True, capture_output=True,
                       env=dict(os.environ, SATTARI_SESSION_ID=f'voc-{tag}-{t}-{os.getpid()}'))
        return t, json.load(open(tr))['reportedLatencySamples']
    with ThreadPoolExecutor(8) as ex: lats = dict(ex.map(run, tracks(split)))
    ei, eo = [], []
    for t in tracks(split):
        mi = ref(WORK / f'v{t}.wav'); mo = ref(WORK / f'v{t}_{tag}.wav', lats[t])
        ti = np.arange(len(mi)) * 0.01; to = np.arange(len(mo)) * 0.01
        for s, e, tgt in agreed(t):
            ci, co = centre(ti, mi, s, e), centre(to, mo, s, e)
            if np.isfinite(ci) and np.isfinite(co) and abs(100 * (ci - tgt)) <= 48:
                ei.append(100 * (ci - tgt)); eo.append(100 * (co - tgt))
        (WORK / f'v{t}_{tag}.wav').unlink(missing_ok=True)
    a, b = np.abs(ei), np.abs(eo)
    s = {'tag': tag, 'split': split, 'mode': mode, 'tracks': len(tracks(split)), 'notes': len(b),
         'input_within_10c': round(100 * float(np.mean(a <= 10)), 1), 'output_within_10c': round(100 * float(np.mean(b <= 10)), 1),
         'output_within_5c': round(100 * float(np.mean(b <= 5)), 1), 'output_median_c': round(float(np.median(b)), 2),
         'output_p95_c': round(float(np.percentile(b, 95)), 1)}
    json.dump(s, open(WORK / f'score_{tag}_{split}.json', 'w'), indent=1); print('SUMMARY', json.dumps(s))

if __name__ == '__main__':
    score(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5:])
