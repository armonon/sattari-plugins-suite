"""AutoKey time-to-follow a key change, synthetic singers (synth_key a cappella clips).

Each test clip = one singer's melody in key A, looped to 27.9 s (3 clips), then the same
singer in key B, looped to 37.2 s (4 clips). Changes: up a half step (same mode), major ->
relative minor, and up a fourth (same mode, the commonest modulation). Time-to-follow =
seconds after the splice until the detector's verdict equals key B and stays there to the end
(raw verdict, and the gated key correction actually uses).

usage: key_change.py make | run <tag> [bench args...]      BENCH=<AutoPitchBench>
"""
import json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np, soundfile as sf
HERE = Path(__file__).parent; SK = HERE / 'synth_key'; OUT = HERE / 'key_change'; OUT.mkdir(exist_ok=True)
NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
VOICES = ['baritone', 'tenor', 'alto', 'soprano']
B = Path(os.environ.get('BENCH', str(HERE.parents[1] / 'build/auto-pitch-neural/AutoPitchBench')))
A_LOOPS, B_LOOPS = 3, 4


def clip(root, minor, voice):
    return SK / 'wav' / f"{NAMES[root]}_{'min' if minor else 'maj'}_{voice}_acappella.wav"


def cases():
    """Every (key A -> key B) pair with the same singer available a cappella in both keys."""
    have = {f.name for f in (SK / 'wav').glob('*_acappella.wav')}
    out = []
    for r in range(12):
        for mi in (False, True):
            rel = ((r + 3) % 12, False) if mi else ((r + 9) % 12, True)
            for kind, kb in (('half', ((r + 1) % 12, mi)), ('rel', rel), ('fourth', ((r + 5) % 12, mi))):
                for voice in VOICES:
                    if clip(r, mi, voice).name in have and clip(*kb, voice).name in have:
                        out.append((f"{kind}_{voice}_{NAMES[r]}{'m' if mi else ''}", voice, (r, mi), kb))
    return out


def make():
    meta = []
    for name, voice, ka, kb in cases():
        a, sr = sf.read(clip(*ka, voice)); b, _ = sf.read(clip(*kb, voice))
        y = np.concatenate([a] * A_LOOPS + [b] * B_LOOPS)
        sf.write(OUT / f'{name}.wav', y, sr, subtype='PCM_16')
        meta.append({'name': name, 'a': list(ka), 'b': list(kb), 'splice_sec': len(a) * A_LOOPS / sr, 'dur': len(y) / sr})
    json.dump(meta, open(OUT / 'manifest.json', 'w'), indent=1); print('made', len(meta))


def run(tag, extra):
    meta = json.load(open(OUT / 'manifest.json'))

    def one(m):
        tr = f"/tmp/kc_{tag}_{m['name']}_{os.getpid()}.json"
        env = dict(os.environ, SATTARI_SESSION_ID=f"kc-{tag}-{m['name']}-{os.getpid()}")
        subprocess.run([str(B), str(OUT / f"{m['name']}.wav"), '--trace', tr, '--auto-key', '--key', 'A', '--scale', 'minor',
                        '--block', '512', *extra], capture_output=True, env=env, check=True)
        fr = json.load(open(tr))['frames']; os.unlink(tr)
        t = np.array([f['timeSec'] for f in fr]); s = m['splice_sec']
        b = (m['b'][0], bool(m['b'][1])); a = (m['a'][0], bool(m['a'][1]))
        raw = np.array([(f['detectedRoot'] % 12, f['detectedScale'] == 1) == b for f in fr])
        used = np.array([(f['usedRoot'] % 12, f['usedScale'] == 1) == b for f in fr])
        before_a = np.array([(f['usedRoot'] % 12, f['usedScale'] == 1) == a for f in fr])

        def follow(ok):
            idx = [i for i in range(len(t)) if t[i] >= s and ok[i:].all()]
            return float(t[idx[0]] - s) if idx else None
        pre = (t > s - 9.3) & (t < s)            # last loop of key A: was key A established?
        raw_a = np.array([(f['detectedRoot'] % 12, f['detectedScale'] == 1) == a for f in fr])
        # Share of time the raw verdict is the sung key: before the splice (key A, 5 s .. splice)
        # and in successive windows after it (key B).
        win = lambda lo, hi, ok: float(ok[(t >= s + lo) & (t < s + hi)].mean())
        shares = [float(raw_a[(t >= 5) & (t < s)].mean()), win(0, 5, raw), win(5, 10, raw), win(10, 20, raw), win(20, 40, raw)]
        return m['name'], follow(raw), follow(used), float(before_a[pre].mean()), shares

    with ThreadPoolExecutor(10) as ex:
        res = list(ex.map(one, meta))
    json.dump(res, open(OUT / f'result_{tag}.json', 'w'), indent=1)
    for kind in ('half', 'rel', 'fourth'):
        r = [x for x in res if x[0].startswith(kind)]
        for label, col in (('raw', 1), ('used', 2)):
            v = [x[col] for x in r if x[col] is not None]
            print(f"{tag} {kind:6s} {label:4s}: followed {len(v)}/{len(r)}, median {np.median(v) if v else float('nan'):5.1f} s, "
                  f"p90 {np.percentile(v, 90) if v else float('nan'):5.1f} s | key A used before splice {100 * np.mean([x[3] for x in r]):.0f}%")
        sh = 100 * np.mean([x[4] for x in r], axis=0)
        print(f"{tag} {kind:6s} raw right share: key A before {sh[0]:.0f}% | key B 0-5 s {sh[1]:.0f}% 5-10 s {sh[2]:.0f}% 10-20 s {sh[3]:.0f}% 20+ s {sh[4]:.0f}%")


if __name__ == '__main__':
    make() if sys.argv[1] == 'make' else run(sys.argv[2], sys.argv[3:])
