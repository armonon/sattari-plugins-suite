"""Real LEAD vocals for AutoKey: Apple Loops vocal phrases (installed with Logic Pro).

Apple Loops CAF files carry Apple's own labels in a metadata chunk: category/subcategory
(Vocals / Male|Female|Choir), key signature, key type (major|minor|both|neither), descriptors.
Selected: Male/Female, Single, Melodic, key type major|minor -> 970 phrases, 207 songs.
Phrases of one song (same pack + title, same key) are concatenated with 0.3 s gaps into one
clip, so AutoKey sees a song-length performance. Split BY SONG: holdout = every 4th song in
sorted order. Audio stays in /Library/Audio/Apple Loops (Logic Pro licence: internal
evaluation only, never redistributed or published); converted copies live in al_work/ (ignored).

usage: al_vocals.py prep | key <tag> <dev|holdout|all> [bench args]   BENCH=<AutoPitchBench>
"""
import json, os, re, struct, subprocess, sys, collections
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np, soundfile as sf
from diskguard import require_free
HERE = Path(__file__).parent; WORK = HERE / 'al_work'; WORK.mkdir(exist_ok=True)
ROOT = Path('/Library/Audio/Apple Loops'); SR = 48000
B = Path(os.environ.get('BENCH', str(HERE.parents[1] / 'build/auto-pitch-neural/AutoPitchBench')))
PC = {'C':0,'C#':1,'Db':1,'D':2,'D#':3,'Eb':3,'E':4,'F':5,'F#':6,'Gb':6,'G':7,'G#':8,'Ab':8,'A':9,'A#':10,'Bb':10,'B':11}
MAJ = [0,2,4,5,7,9,11]; MIN = [0,2,3,5,7,8,10]
def pcs(r, minor): return frozenset((r + i) % 12 for i in (MIN if minor else MAJ))

def meta(path):
    d = {}
    with open(path, 'rb') as f:
        if f.read(4) != b'caff': return d
        f.read(4)
        while True:
            h = f.read(12)
            if len(h) < 12: break
            typ = h[:4]; size = struct.unpack('>q', h[4:])[0]
            if typ == b'data' or size < 0: break
            raw = f.read(size)
            if typ in (b'uuid', b'info'):
                body = raw[16:] if typ == b'uuid' else raw
                if len(body) < 4: continue
                n = struct.unpack('>I', body[:4])[0]; parts = body[4:].split(b'\0')
                if 0 < n <= 64 and len(parts) >= 2 * n:
                    for i in range(0, 2 * n, 2):
                        k = parts[i].decode('utf8', 'ignore')
                        if k.isprintable() and len(k) < 40: d[k] = parts[i + 1].decode('utf8', 'ignore')
    return d

def prep():
    require_free()
    songs = collections.defaultdict(list)
    for dp, _, fs in os.walk(ROOT):
        for fn in sorted(fs):
            if not fn.endswith('.caf'): continue
            p = Path(dp) / fn; d = meta(p); desc = d.get('descriptors', '')
            if (d.get('category') == 'Vocals' and d.get('subcategory') in ('Male', 'Female')
                    and d.get('key type') in ('major', 'minor') and d.get('key signature') in PC
                    and 'Single' in desc and 'Melodic' in desc):
                title = re.sub(r'\s*\d+$', '', p.stem)
                songs[(p.parent.name, title, d['key signature'], d['key type'], d['subcategory'])].append((str(p), 'Dry' in desc))
    items = []
    for i, (k, files) in enumerate(sorted(songs.items())):
        pack, title, root, mode, sex = k
        name = re.sub(r'[^A-Za-z0-9]+', '_', f'{pack}_{title}_{root}{mode}')[:80]
        out = WORK / f'{name}.wav'
        if not out.exists():
            chunks = []
            for f, _ in files:
                tmp = WORK / '_tmp.wav'
                subprocess.run(['afconvert', '-f', 'WAVE', '-d', f'LEI24@{SR}', '-c', '1', f, str(tmp)], check=True, capture_output=True)
                x, _ = sf.read(tmp); chunks += [x, np.zeros(int(0.3 * SR))]
            sf.write(out, np.concatenate(chunks), SR, subtype='PCM_24'); (WORK / '_tmp.wav').unlink(missing_ok=True)
        items.append({'name': name, 'wav': str(out), 'root': PC[root], 'minor': mode == 'minor', 'sex': sex,
                      'pack': pack, 'phrases': len(files), 'dry': all(dr for _, dr in files),
                      'split': 'holdout' if i % 4 == 3 else 'dev'})
    json.dump(items, open(HERE / 'al_manifest.json', 'w'), indent=1)
    print('songs', len(items), collections.Counter(x['split'] for x in items), collections.Counter(x['sex'] for x in items))

def key(tag, split, extra):
    require_free()
    items = [x for x in json.load(open(HERE / 'al_manifest.json')) if split in ('all', x['split'])]
    def one(m):
        tr = f'/tmp/al_{tag}_{m["name"]}_{os.getpid()}.json'
        env = dict(os.environ, SATTARI_SESSION_ID=f'al-{tag}-{m["name"]}-{os.getpid()}')
        subprocess.run([str(B), m['wav'], '--trace', tr, '--auto-key', '--manual-key-untouched', '--block', '512', *extra],
                       capture_output=True, env=env, check=True)
        fr = json.load(open(tr))['frames']; os.unlink(tr)
        truth = pcs(m['root'], m['minor']); v = [f for f in fr if f['voiced']]
        used = collections.Counter('chrom' if f['usedScale'] == 2 else ('right' if f['usedScale'] in (0, 1) and pcs(f['usedRoot'], f['usedScale'] == 1) == truth else 'wrong') for f in v)
        raw = np.mean([f['detectedScale'] in (0, 1) and pcs(f['detectedRoot'], f['detectedScale'] == 1) == truth for f in v])
        last = fr[-1]; final = last['detectedScale'] in (0, 1) and pcs(last['detectedRoot'], last['detectedScale'] == 1) == truth
        n = max(1, len(v)); return m, {k: used[k] / n for k in ('right', 'wrong', 'chrom')}, float(raw), bool(final)
    with ThreadPoolExecutor(8) as ex: res = list(ex.map(one, items))
    json.dump([(m['name'], u, r, f) for m, u, r, f in res], open(WORK / f'key_{tag}_{split}.json', 'w'), indent=1)
    for grp in ('all', 'Male', 'Female'):
        rr = [x for x in res if grp in ('all', x[0]['sex'])]
        u = {k: 100 * np.mean([x[1][k] for x in rr]) for k in ('right', 'wrong', 'chrom')}
        print(f"{tag} {split} {grp:6s} n={len(rr):3d} | used right {u['right']:4.1f}% wrong {u['wrong']:4.1f}% chromatic {u['chrom']:4.1f}% "
              f"| raw right {100*np.mean([x[2] for x in rr]):4.1f}% | final verdict right {sum(x[3] for x in rr)}/{len(rr)}")

if __name__ == '__main__':
    prep() if sys.argv[1] == 'prep' else key(sys.argv[2], sys.argv[3], sys.argv[4:])
