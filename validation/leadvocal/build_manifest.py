"""Builds validation/leadvocal/manifest.json: the professional LEAD-VOCAL corpus for Auto Pitch /
Auto Key benchmarking (licence per item, singer+song split, sealed holdout).

Sources (audio lives under ~/datasets, never in git):
  n20emv2   ~/datasets/n20emv2         CC BY-SA 4.0 (Zenodo record label), whole-song lead vocals, 16 kHz
  vocadito  ~/datasets/vocadito        CC BY 4.0, solo singing excerpts, 44.1 kHz
  al        /Library/Audio/Apple Loops Logic Pro licence, internal evaluation only (key labels only)
  csd       ~/datasets/choral_singing  CC BY 4.0, sopranos only, secondary adversarial set

Every derived number is computed here from annotations or a reference f0; nothing is hand-tagged
except song titles (identified from the N20EM v1 lyric metadata) and the genre of the original
recording of each identified title. Tags that cannot be derived are left null ("unknown").

usage (needs torch/torchcrepe for the reference f0 of N20EM + Apple Loops):
  ~/.venvs/sattari-audio/bin/python validation/leadvocal/build_manifest.py
Reference f0 and decoded Apple Loops are cached in ~/datasets/leadvocal_cache (~50 MB).
"""
import collections, csv, datetime, hashlib, json, os, re, shutil, struct, subprocess, sys, tempfile
from pathlib import Path
import numpy as np, soundfile as sf, warnings
warnings.filterwarnings('ignore', category=RuntimeWarning)

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
ACC = REPO / 'validation/accuracy_20260923'
D = Path.home() / 'datasets'
N20, VOC, CSD = D / 'n20emv2', D / 'vocadito', D / 'choral_singing/ChoralSingingDataset'
ALROOT = Path('/Library/Audio/Apple Loops')
CACHE = D / 'leadvocal_cache'
SEED = 20260927
HOP = 0.01
FLOOR_GB = 25

NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
PC = {'C': 0, 'C#': 1, 'Db': 1, 'D': 2, 'D#': 3, 'Eb': 3, 'E': 4, 'F': 5, 'F#': 6, 'Gb': 6, 'G': 7,
      'G#': 8, 'Ab': 8, 'A': 9, 'A#': 10, 'Bb': 10, 'B': 11}
TEMP_MAJ = np.array([5, 2, 3.5, 2, 4.5, 4, 2, 4.5, 2, 3.5, 1.5, 4.])
TEMP_MIN = np.array([5, 2, 3.5, 4.5, 2, 4, 2, 4.5, 3.5, 2, 1.5, 4.])
AMBIGUOUS_MARGIN = 0.05   # r(best) - r(runner-up) below this => key_ambiguous

LIC = {
    'n20emv2': ('CC-BY-SA-4.0', 'https://creativecommons.org/licenses/by-sa/4.0/', 'internal-eval'),
    'vocadito': ('CC-BY-4.0', 'https://creativecommons.org/licenses/by/4.0/', 'internal-eval'),
    'csd': ('CC-BY-4.0', 'https://creativecommons.org/licenses/by/4.0/', 'internal-eval'),
    'al': ('Apple Logic Pro software licence (Apple Loops)', 'https://www.apple.com/legal/sla/',
           'internal-eval-logic-licence'),
}

# N20EM song index -> (canonical song id, title, genre of the original recording).
# Titles identified from the first lyric lines in the N20EM v1 metadata (zenodo.org/records/7545968,
# metadata_split_by_song.json); songs are the 20 of the NUS-48E corpus. Genre = the original
# recording's genre, not a property measured from these amateur covers.
N20_SONGS = {
    1: ('edelweiss', 'Edelweiss', 'musical-theatre'), 2: ('do-re-mi', 'Do-Re-Mi', 'musical-theatre'),
    3: ('jingle-bells', 'Jingle Bells', 'traditional-holiday'), 4: ('silent-night', 'Silent Night', 'traditional-holiday'),
    5: ('wonderful-tonight', 'Wonderful Tonight (Eric Clapton)', 'rock'), 6: ('moon-river', 'Moon River', 'standard'),
    7: ('rhythm-of-the-rain', 'Rhythm of the Rain (The Cascades)', 'pop'), 8: ('i-have-a-dream', 'I Have a Dream (ABBA)', 'pop'),
    9: ('love-me-tender', 'Love Me Tender', 'pop'), 10: ('twinkle-twinkle', 'Twinkle Twinkle Little Star', 'children'),
    11: ('you-are-my-sunshine', 'You Are My Sunshine', 'country-folk'), 12: ('a-little-love', 'A Little Love (Fiona Fung)', 'pop'),
    13: ('proud-of-you', 'Proud of You (Fiona Fung)', 'pop'), 14: ('lemon-tree', "Lemon Tree (Fool's Garden)", 'pop'),
    15: ('can-you-feel-the-love-tonight', 'Can You Feel the Love Tonight (Elton John)', 'pop'),
    16: ('far-away-from-home', 'Far Away From Home (Groove Coverage)', 'electronic'),
    17: ('seasons-in-the-sun', 'Seasons in the Sun (Terry Jacks)', 'pop'), 18: ('the-show', 'The Show (Lenka)', 'indie'),
    19: ('the-rose', 'The Rose (Bette Midler)', 'pop'), 20: ('right-here-waiting', 'Right Here Waiting (Richard Marx)', 'pop'),
}
# vocadito tracks that share a melody with another item (song-level leakage links).
VOC_SONG = {12: 'love-me-tender', 32: 'twinkle-twinkle', 10: 'rattlin-bog', 34: 'rattlin-bog'}
VOC_GENRE = {12: 'pop', 15: 'country-folk', 14: 'children', 20: 'traditional-holiday', 38: 'traditional-holiday',
             22: 'children', 25: 'children', 32: 'children', 40: 'pop', 39: 'standard', 19: 'standard',
             21: 'country-folk', 11: 'country-folk', 13: 'country-folk', 10: 'country-folk', 34: 'country-folk'}
AL_GENRE = {'Rock/Blues': 'rock', 'Modern RnB': 'r&b', 'Hip Hop/RnB': 'r&b', 'Urban': 'urban (apple label: r&b/hip-hop)',
            'Electronic/Dance': 'electronic', 'House': 'electronic', 'Techno': 'electronic', 'Electronic Pop': 'electronic',
            'Hip Hop': 'hip-hop', 'Country/Folk': 'country-folk', 'Jazz': 'jazz', 'World/Ethnic': 'world',
            'Orchestral': 'orchestral', 'Experimental': 'experimental', 'Other Genre': None}


def require_free():
    free = shutil.disk_usage('/').free / 2**30
    if free < FLOOR_GB:
        sys.exit(f'ABORT: {free:.1f} GB free < {FLOOR_GB} GB floor')


def midi(hz):
    return 69 + 12 * np.log2(np.asarray(hz, float) / 440.0)


# ---------------------------------------------------------------- reference f0
def crepe_tiny(y16):
    import torch, torchcrepe
    np.random.seed(0)
    p, per = torchcrepe.predict(torch.tensor(y16, dtype=torch.float32)[None], 16000, hop_length=160, fmin=60, fmax=1100,
                                model='tiny', decoder=torchcrepe.decode.viterbi, return_periodicity=True,
                                batch_size=2048, device='mps')
    per = torchcrepe.filter.median(per, 3)
    p, per = p[0].numpy(), per[0].numpy()
    rms = np.array([np.sqrt(np.mean(y16[i * 160:i * 160 + 1024] ** 2) + 1e-12) for i in range(len(p))])
    return np.where((per > 0.5) & (rms > 10 ** (-45 / 20)), midi(p), np.nan)


def load16(path):
    y, sr = sf.read(str(path), dtype='float32', always_2d=True)
    y = y.mean(axis=1)
    if sr != 16000:
        import librosa
        y = librosa.resample(y, orig_sr=sr, target_sr=16000)
    return y


def cached_f0(key, make):
    CACHE.mkdir(parents=True, exist_ok=True)
    c = CACHE / f'{re.sub(r"[^A-Za-z0-9._-]+", "_", key)}.npy'
    if c.exists():
        return np.load(c)
    require_free()
    m = make(); np.save(c, m.astype(np.float32)); return m


def annotated_f0(path, sep=','):
    a = np.loadtxt(path, delimiter=sep if sep != ' ' else None)
    t, hz = a[:, 0], a[:, 1]
    grid = np.arange(0, t[-1] + HOP / 2, HOP)
    idx = np.clip(np.searchsorted(t, grid), 0, len(t) - 1)
    h = hz[idx]
    return np.where(h > 0, midi(np.where(h > 0, h, 1)), np.nan)


# ---------------------------------------------------------------- key
def key_fit(notes):
    """Duration-weighted Temperley fit of (start, end, midi) notes. Returns dict or None."""
    h = np.zeros(12)
    for s, e, m in notes:
        h[int(round(m)) % 12] += max(0.0, e - s)
    if h.sum() <= 0 or np.count_nonzero(h) < 3:
        return None
    return key_from_hist(h)


def key_from_hist(h):
    sc = sorted(((float(np.corrcoef(h, np.roll(p, r))[0, 1]), r, mi) for r in range(12)
                 for mi, p in ((0, TEMP_MAJ), (1, TEMP_MIN))), reverse=True)
    (r1, k1, m1), (r2, k2, m2) = sc[0], sc[1]
    rel = (m1 != m2) and (k2 == ((k1 + 3) % 12 if m1 else (k1 + 9) % 12))
    return {'key': f"{NAMES[k1]} {'minor' if m1 else 'major'}", 'r': round(r1, 3), 'margin': round(r1 - r2, 3),
            'runner_up': f"{NAMES[k2]} {'minor' if m2 else 'major'}", 'runner_up_is_relative': bool(rel),
            'ambiguous': bool(r1 - r2 < AMBIGUOUS_MARGIN)}


def key_segments(notes, dur, win=30.0, hop=10.0):
    """Windowed key fits; returns segments where a different key holds for >= 3 consecutive
    confident windows (time-varying truth). Single segment when the song keeps one key."""
    if dur < 2 * win:
        return None
    ks = []
    for t0 in np.arange(0, max(hop, dur - win + hop), hop):
        f = key_fit([(max(s, t0), min(e, t0 + win), m) for s, e, m in notes if e > t0 and s < t0 + win])
        ks.append((t0, f['key'] if f and not f['ambiguous'] else None))
    segs, cur = [], None
    for t0, k in ks:
        if k is None:
            continue
        if cur and cur['key'] == k:
            cur['end'] = round(t0 + win, 1); cur['windows'] += 1
        else:
            cur = {'key': k, 'start': round(t0, 1), 'end': round(t0 + win, 1), 'windows': 1}; segs.append(cur)
    stable = [s for s in segs if s['windows'] >= 3]
    return stable


def modulation_candidates(global_key, segs):
    """Stable window segments in the same mode whose tonic is 1-2 semitones from the global key
    (the usual pop 'lift'). Diatonic neighbours (IV, V, relative) are not flagged: in amateur
    whole-song singing they are usually a melody that dwells on other degrees, not a modulation."""
    if not segs or not global_key:
        return []
    gt, gm = global_key.split(); out = []
    for s in segs:
        t, m = s['key'].split()
        d = (NAMES.index(t) - NAMES.index(gt)) % 12
        if m == gm and d in (1, 2, 10, 11) and s['end'] - s['start'] >= 50:
            out.append({'key': s['key'], 'start': s['start'], 'end': s['end'], 'shift_semitones': d if d < 6 else d - 12})
    return out


# ---------------------------------------------------------------- note / f0 statistics
def note_stats(notes):
    if not notes:
        return None
    L = np.array([e - s for s, e, _ in notes])
    tot = L.sum()
    return {'count': len(L), 'median_len_s': round(float(np.median(L)), 3), 'p10_len_s': round(float(np.percentile(L, 10)), 3),
            'p90_len_s': round(float(np.percentile(L, 90)), 3),
            'frac_time_notes_ge_1s': round(float(L[L >= 1.0].sum() / tot), 3),
            'frac_notes_lt_150ms': round(float(np.mean(L < 0.15)), 3),
            'notes_per_note_second': round(float(len(L) / tot), 2),
            'median_midi_dur_weighted': round(float(weighted_median([m for *_, m in notes], L)), 2),
            'range_p5_p95_midi': [round(float(np.percentile([m for *_, m in notes], q)), 1) for q in (5, 95)]}


def weighted_median(x, w):
    o = np.argsort(x); x, w = np.asarray(x)[o], np.asarray(w)[o]
    return x[np.searchsorted(np.cumsum(w), 0.5 * w.sum())]


def sustained_segments(f0, notes=None, min_len=0.5):
    """(start_frame, end_frame) regions to analyse for vibrato: note cores when notes are known,
    else voiced runs of the reference f0."""
    n = len(f0); out = []
    if notes:
        for s, e, _ in notes:
            a, b = int((s + 0.10) / HOP), min(n, int((e - 0.05) / HOP))
            if (b - a) * HOP >= min_len and np.isfinite(f0[a:b]).mean() > 0.9:
                out.append((a, b))
    else:
        v = np.isfinite(f0); i = 0
        while i < n:
            if not v[i]:
                i += 1; continue
            j = i
            while j < n and v[j]:
                j += 1
            if (j - i) * HOP >= min_len + 0.15:
                out.append((i + 10, j - 5))
            i = j
    return out


def vibrato(f0, notes=None):
    """Per-item vibrato from the reference/annotated f0. For each sustained segment >= 0.5 s the
    pitch is detrended with a 0.4 s running median (edge-preserving, so note steps don't count),
    and the residual spectrum is searched for a 3.5-9 Hz peak. A segment 'has vibrato' when that
    band holds >= 50% of the residual power above 2 Hz and the extent is >= 20 c peak-to-peak.
    extent_cents = peak-to-peak = 2*sqrt(2)*rms of the 3-10 Hz band."""
    from scipy.ndimage import median_filter
    from scipy.signal import butter, sosfiltfilt
    segs = sustained_segments(f0, notes)
    if len(segs) < 3:
        return {'segments': len(segs), 'bucket': 'unknown'}
    sos = butter(2, [3.0, 10.0], btype='band', fs=1 / HOP, output='sos')
    res = []
    for a, b in segs:
        x = f0[a:b].copy()
        if np.isnan(x).any():
            ok = np.isfinite(x); x = np.interp(np.arange(len(x)), np.flatnonzero(ok), x[ok])
        c = 100 * (x - median_filter(x, size=41, mode='nearest'))
        if len(c) < 50:
            continue
        spec = np.abs(np.fft.rfft((c - c.mean()) * np.hanning(len(c)), 1024)) ** 2
        fr = np.fft.rfftfreq(1024, HOP)
        band = (fr >= 3.5) & (fr <= 9.0); above = fr >= 2.0
        frac = spec[band].sum() / max(spec[above].sum(), 1e-9)
        bp = sosfiltfilt(sos, c) if len(c) > 15 else c
        ext = 2 * np.sqrt(2) * np.sqrt(np.mean(bp[5:-5] ** 2))
        res.append(((b - a) * HOP, frac >= 0.5 and ext >= 20, float(fr[band][np.argmax(spec[band])]), float(ext)))
    if len(res) < 3:
        return {'segments': len(res), 'bucket': 'unknown'}
    w = np.array([r[0] for r in res]); vib = np.array([r[1] for r in res])
    frac_t = float(w[vib].sum() / w.sum())
    out = {'segments': len(res), 'frac_sustained_time_with_vibrato': round(frac_t, 3)}
    if vib.sum() >= 2:
        out['rate_hz_median'] = round(float(np.median([r[2] for r in res if r[1]])), 2)
        out['extent_cents_pp_median'] = round(float(np.median([r[3] for r in res if r[1]])), 1)
    if frac_t < 0.15 or vib.sum() < 2:
        out['bucket'] = 'none'
    else:
        e = out['extent_cents_pp_median']
        out['bucket'] = 'narrow' if e < 50 else ('medium' if e < 100 else 'wide')
    return out


def ornaments(f0, notes):
    """Scoops (onset starts >= 80 c below the note core and converges within 200 ms) and
    portamento (a continuously voiced transition between notes >= 2 st apart that spends >= 80 ms
    between the two pitches). Heuristic; thresholds in README."""
    if not notes or f0 is None:
        return None
    n = len(f0); sc = tot = 0; port = pt = 0
    for s, e, m in notes:
        a, b = int(s / HOP), int(e / HOP)
        if e - s < 0.25 or b >= n:
            continue
        core = f0[int((s + 0.1) / HOP):b - 5]
        on = f0[a + 2:a + 6]
        if np.isfinite(core).mean() < 0.8 or np.isfinite(on).sum() < 2:
            continue
        tot += 1; cm = np.nanmedian(core)
        if 100 * (cm - np.nanmedian(on)) >= 80 and np.nanmedian(f0[a + 15:a + 20]) > cm - 0.5:
            sc += 1
    for (s1, e1, m1), (s2, e2, m2) in zip(notes, notes[1:]):
        if s2 - e1 > 0.05 or abs(m2 - m1) < 2:
            continue
        a, b = int((e1 - 0.15) / HOP), int((s2 + 0.15) / HOP)
        if b >= n:
            continue
        seg = f0[a:b]
        if np.isfinite(seg).mean() < 0.95:
            continue
        pt += 1
        lo, hi = min(m1, m2) + 0.5, max(m1, m2) - 0.5
        if ((seg > lo) & (seg < hi)).sum() * HOP >= 0.08:
            port += 1
    return {'scoop_frac': round(sc / tot, 3) if tot else None, 'scoop_notes_tested': tot,
            'portamento_frac': round(port / pt, 3) if pt else None, 'transitions_tested': pt}


REG = {'M': (50, 57), 'F': (60, 67)}   # median MIDI: < lo = low, > hi = high (M: D3 / A3, F: C4 / G4)


def register(med, sex):
    if med is None or sex not in REG:
        return None
    lo, hi = REG[sex]
    return 'low' if med < lo else ('high' if med > hi else 'mid')


def pace(ns):
    if not ns:
        return None
    tags = []
    if ns['median_len_s'] < 0.25: tags.append('fast')
    if ns['median_len_s'] > 0.5: tags.append('slow')
    if ns['frac_time_notes_ge_1s'] >= 0.25: tags.append('sustained')
    return tags or ['moderate']


# ---------------------------------------------------------------- sources
def n20em_items():
    ann = json.load(open(N20 / 'annotations.json'))
    out = []
    for sid, a in sorted(ann.items()):
        singer, song = sid.split('-')
        arr_f = song.endswith('f'); k = int(song.rstrip('f'))
        cid, title, genre = N20_SONGS[k]
        voc = N20 / 'audio' / sid / 'vocals.wav'
        if not voc.exists():
            print('missing audio', sid); continue
        info = sf.info(str(voc))
        notes = [(float(s), float(e), float(m)) for s, e, m in a['midi']]
        f0 = cached_f0(f'n20em_{sid}_crepetiny', lambda: crepe_tiny(load16(voc)))
        out.append(dict(id=f'n20emv2:{sid}', source='n20emv2', path=str(voc).replace(str(Path.home()), '~'),
                        accomp_path=str(N20 / 'audio' / sid / 'accomp.wav').replace(str(Path.home()), '~'),
                        singer_id=f'n20emv2:{singer}', song_id=cid, song_title=title,
                        song_title_method='identified from N20EM v1 lyric metadata', arrangement='female' if arr_f else 'default',
                        genre=genre, genre_method='genre of the original recording of the identified title',
                        sample_rate=info.samplerate, duration_s=round(info.duration, 2), sr_limited=info.samplerate < 44100,
                        notes=notes, notes_available='expert-corrected (N20EMv2 annotations.json)', f0=f0,
                        f0_available='reference: CREPE-tiny (viterbi) computed here; dataset ships no f0',
                        has_bleed=False, bleed_note='isolated vocal; aligned accompaniment ships separately (accomp.wav)',
                        sex=None, training='untrained to amateur (dataset-level, per the N20EM paper)',
                        orig_split=a['split'], prior_use=None, language='English', set='primary'))
    return out


def vocadito_items():
    meta = {r['track_id']: r for r in csv.DictReader(open(VOC / 'vocadito_metadata.csv'))}
    singers = sorted({r['singer_id'] for r in meta.values()}, key=lambda s: int(s[1:]))
    old_hold = set(singers[2::3])   # vocadito_bench.py holdout
    out = []
    for t in sorted(meta, key=int):
        r = meta[t]; ti = int(t); p = VOC / f'Audio/vocadito_{t}.wav'; info = sf.info(str(p))
        notes = [(float(s), float(s) + float(d), float(midi(float(f)))) for s, f, d in
                 csv.reader(open(VOC / f'Annotations/Notes/vocadito_{t}_notesA1.csv'))]
        out.append(dict(id=f'vocadito:{t}', source='vocadito', path=str(p).replace(str(Path.home()), '~'),
                        singer_id=f'vocadito:{r["singer_id"]}', song_id=VOC_SONG.get(ti, f'vocadito-{t}'),
                        song_title=(open(VOC / f'Annotations/Lyrics/vocadito_{t}_lyrics.txt').readline().strip()[:60] or None),
                        song_title_method='first lyric line', genre=VOC_GENRE.get(ti),
                        genre_method='genre of the identified traditional/popular song' if ti in VOC_GENRE else None,
                        sample_rate=info.samplerate, duration_s=round(info.duration, 2), sr_limited=False,
                        notes=notes, notes_available='manual, 2 annotators (A1 used; A2 available)',
                        f0=annotated_f0(VOC / f'Annotations/F0/vocadito_{t}_f0.csv'), f0_available='annotated (dataset)',
                        has_bleed=False, bleed_note='solo, phone/consumer capture', sex=None,
                        training='mixed (dataset-level)', language=r['language'],
                        prior_use='vocadito_bench holdout (scored, not tuned)' if r['singer_id'] in old_hold else 'vocadito_bench dev (tuned on)',
                        set='primary'))
    return out


def csd_items():
    out = []
    for piece, title in (('ER', 'El Rossinyol'), ('LI', 'Locus Iste'), ('ND', "Nino Dios d'Amor Herido")):
        a = np.loadtxt(CSD / f'CSD_{piece}_soprano_notes.lab')
        notes = [(float(s), float(s + d), float(midi(f))) for s, f, d in a]
        for i in range(1, 5):
            p = CSD / f'CSD_{piece}_soprano_{i}.wav'; info = sf.info(str(p))
            out.append(dict(id=f'csd:{piece}_soprano_{i}', source='csd', path=str(p).replace(str(Path.home()), '~'),
                            singer_id=f'csd:soprano_{i}', song_id=f'csd-{piece}', song_title=title, song_title_method='dataset',
                            genre='choral', genre_method='dataset', sample_rate=info.samplerate, duration_s=round(info.duration, 2),
                            sr_limited=False, notes=notes, notes_available='manual (section-level, shared by the 4 sopranos)',
                            f0=annotated_f0(CSD / f'CSD_{piece}_soprano_{i}.f0', sep=' '), f0_available='annotated (Tony, corrected)',
                            has_bleed=True, bleed_note='other choir singers audible in some tracks (dataset README)',
                            sex=None, voice_part='soprano', training='professional choir',
                            language={'ER': 'Catalan', 'LI': 'Latin', 'ND': 'Spanish'}[piece],
                            prior_use='csd_bench dev/holdout (scored and tuned since 2026-09-23)', set='secondary'))
    return out


def al_meta(path):
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


AL_NAMED = ('Jam Pack Voices', 'Vox Melodics')
AL_ROLE_WORDS = r'\s+(?:\d\d\s+)?(?:Harmony.*|Ad Lib|Hook|Oh Ah.*|Verse|Improv|Lyric|Melody|Background)$'


def al_identity(pack, title):
    """(singer proxy, song id, role) of an Apple Loops item. Jam Pack Voices / Vox Melodics titles
    start with the performer persona ('Carla Melody', 'Max Hiding 03 Harmony High'); for other packs
    the pack is the only singer proxy. Vox Melodics titles carry the song name after the persona."""
    t = title.strip()
    role = ('harmony' if 'Harmony' in t else 'background' if 'Background' in t else 'ad-lib' if 'Ad Lib' in t
            or 'Oh Ah' in t else 'lead')
    if pack in AL_NAMED:
        persona = t.split()[0]
        if pack == 'Vox Melodics':
            return f'{pack}/{persona}', f'{pack}/{re.sub(AL_ROLE_WORDS, "", t)}', role
        return f'{pack}/{persona}', f'{pack}/{t}', role
    return pack, f'{pack}/{t}', role


def al_items():
    """Rebuilds al_vocals.py's song grouping from the CAFs (read-only) to recover source paths,
    Apple's genre/descriptor labels and durations; ids match al_manifest.json names."""
    old = {x['name']: x for x in json.load(open(ACC / 'al_manifest.json'))}
    songs = collections.defaultdict(list)
    for dp, _, fs in os.walk(ALROOT):
        for fn in sorted(fs):
            if not fn.endswith('.caf'): continue
            p = Path(dp) / fn; d = al_meta(p); desc = d.get('descriptors', '')
            if (d.get('category') == 'Vocals' and d.get('subcategory') in ('Male', 'Female')
                    and d.get('key type') in ('major', 'minor') and d.get('key signature') in PC
                    and 'Single' in desc and 'Melodic' in desc):
                title = re.sub(r'\s*\d+$', '', p.stem)
                songs[(p.parent.name, title, d['key signature'], d['key type'], d['subcategory'])].append((p, d))
    out = []
    for k, files in sorted(songs.items()):
        pack, title, root, mode, sex = k
        name = re.sub(r'[^A-Za-z0-9]+', '_', f'{pack}_{title}_{root}{mode}')[:80]
        if name not in old:
            continue
        f0s, dur = [], 0.0
        for p, _ in files:
            def mk(p=p):
                with tempfile.TemporaryDirectory() as td:
                    w = Path(td) / 'x.wav'
                    subprocess.run(['afconvert', '-f', 'WAVE', '-d', 'LEF32@16000', '-c', '1', str(p), str(w)], check=True, capture_output=True)
                    return crepe_tiny(sf.read(str(w), dtype='float32')[0])
            f = cached_f0(f'al_{pack}_{p.stem}_crepetiny', mk)
            f0s += [f, np.full(30, np.nan)]; dur += len(f) * HOP + 0.3
        f0 = np.concatenate(f0s)
        genres = collections.Counter(d.get('genre') for _, d in files).most_common(1)[0][0]
        descs = sorted({x.strip() for _, d in files for x in d.get('descriptors', '').split(',') if x.strip()})
        o = old[name]
        singer, song, role = al_identity(pack, title)
        out.append(dict(id=f'al:{name}', source='al', path=f'$AL_WORK/{name}.wav',
                        path_note='concatenation made by accuracy_20260923/al_vocals.py prep (phrases + 0.3 s gaps); source CAFs listed',
                        source_paths=[str(p) for p, _ in files], singer_id=f'al:{singer}',
                        singer_id_method=('persona name in the loop title (Jam Pack Voices / Vox Melodics)' if pack in AL_NAMED
                                          else 'pack name (proxy; one pack may hold several singers)'),
                        song_id=f'al:{song}', song_title=title, song_title_method='Apple Loop file name', role=role,
                        genre=AL_GENRE.get(genres), genre_label_raw=genres, genre_method='Apple Loops genre label',
                        apple_descriptors=descs, sample_rate=44100, codec='AAC in CAF (lossy)', duration_s=round(dur, 2),
                        sr_limited=False, notes=None, notes_available=False, f0=f0,
                        f0_available='reference: CREPE-tiny (viterbi) computed here', has_bleed=False,
                        bleed_note='dry' if o['dry'] else 'processed (Apple descriptor), no backing', sex=sex[0],
                        sex_method='Apple subcategory label', label_key=f"{NAMES[o['root']]} {'minor' if o['minor'] else 'major'}",
                        phrases=len(files), training='professional session singers (Apple Loops; presumed)',
                        prior_use='al_vocals holdout (scored, not tuned)' if o['split'] == 'holdout' else 'al_vocals dev (tuned on)',
                        language=None, set='primary' if role in ('lead', 'ad-lib') else 'secondary'))
    return out


# ---------------------------------------------------------------- sex inference (N20EM, vocadito)
def infer_sex(items):
    """Sex is not in N20EM or vocadito metadata. Inferred per singer from the duration-weighted
    median annotated MIDI over all of the singer's items: < 59 => M, > 59 => F. N20EM singers
    form two groups (medians 50-58 and 60-65, nothing in between): 17 vs 14 ids against the
    paper's 17 male / 13 female (31 ids for 30 people).
    Stored in sex_inferred only; the 'sex' field stays null."""
    by = collections.defaultdict(list)
    for it in items:
        if it['source'] in ('n20emv2', 'vocadito') and it.get('note_stats'):
            by[it['singer_id']].append((it['note_stats']['median_midi_dur_weighted'], sum(e - s for s, e, _ in it['notes'])))
    res = {}
    for s, v in by.items():
        med = float(weighted_median([a for a, _ in v], [b for _, b in v]))
        res[s] = ('M' if med < 59 else 'F' if med > 59 else None, round(med, 2))
    return res


def pc_hist(notes):
    h = np.zeros(12)
    for st, e, m in notes:
        h[int(round(m)) % 12] += max(0.0, e - st)
    return h


def arrangement_keys(items, r_min=0.85):
    """N20EM singers sang to backing tracks in a predetermined key per arrangement (the paper:
    male or female arrangement per song). Within each song, performances are grouped by their
    duration-weighted pitch-class histogram (greedy, item order, join a group when r >= 0.85 with
    the group's pooled histogram); the key truth of a group is the Temperley fit of the POOLED
    notes of all its singers, which removes most single-performance fit errors (relative /
    dominant confusions). Groups of one keep their own fit. The item's own fit is kept as
    key_fit_item. Modulation candidates shared by >= half of a group (>= 2) are 'confirmed'."""
    by = collections.defaultdict(list)
    for it in items:
        if it['source'] == 'n20emv2':
            by[it['song_id']].append(it)
    for song, its in by.items():
        groups = []
        for it in sorted(its, key=lambda x: x['id']):
            h = pc_hist(it['notes'])
            for g in groups:
                if np.corrcoef(h, g['h'])[0, 1] >= r_min:
                    g['items'].append(it); g['h'] = g['h'] + h / h.sum(); break
            else:
                groups.append({'items': [it], 'h': h / h.sum()})
        for gi, g in enumerate(groups):
            n = len(g['items'])
            pooled = key_from_hist(g['h']) if n >= 2 else None
            cands = collections.Counter((c['key'], c['shift_semitones']) for i in g['items'] for c in
                                        {(x['key'], x['shift_semitones']): x for x in i['modulation_candidates']}.values())
            conf = {k for k, c in cands.items() if n >= 2 and c >= max(2, n / 2)}
            for it in g['items']:
                it['key_fit_item'] = it['key_fit']
                it['arrangement_group'] = {'id': f'{song}#{gi + 1}', 'singers': n,
                                           'item_fit_agrees_with_pooled': bool(pooled and it['key_fit']['key'] == pooled['key'])}
                if pooled:
                    it['key_fit'] = pooled; it['key_truth'] = pooled['key']; it['key_ambiguous'] = pooled['ambiguous']
                    it['key_truth_method'] = ('derived-from-notes: Temperley fit of the pooled notes of the %d singers of this '
                                              'song+arrangement (same backing-track key)' % n)
                it['modulation_confirmed'] = [c for c in it['modulation_candidates'] if (c['key'], c['shift_semitones']) in conf]


# ---------------------------------------------------------------- split
def make_split(items):
    """Singer x song co-assignment. Every singer and every song gets one label in {H, V, D};
    an item is kept iff its singer's and its song's labels agree, otherwise it is excluded
    (split='excluded-crossover'), so no singer and no song ever appears in two splits. Items that
    earlier benchmarks tuned on cannot enter the sealed holdout (split='excluded-prior-tuning').
    Labels are found by simulated annealing (seeds SEED..SEED+3, best kept) maximising the kept
    fraction per source while pulling each source's singer shares AND kept-minute shares toward
    H 20% / V 15% / D 65% and keeping the female/male and register mix of H and V close to the
    source's overall mix."""
    prim = [it for it in items if it['source'] != 'csd']
    singers = sorted({it['singer_id'] for it in prim}); songs = sorted({it['song_id'] for it in prim})
    si = {s: i for i, s in enumerate(singers)}; so = {s: i for i, s in enumerate(songs)}
    S = np.array([si[it['singer_id']] for it in prim]); G = np.array([so[it['song_id']] for it in prim])
    srcs = sorted({it['source'] for it in prim}); SRC = np.array([srcs.index(it['source']) for it in prim])
    tuned = np.array(['tuned on' in (it.get('prior_use') or '') for it in prim])
    sexv = np.array([{'F': 1.0, 'M': 0.0}.get(it.get('sex') or it.get('sex_inferred'), np.nan) for it in prim])
    regv = np.array([{'low': 0.0, 'mid': 0.5, 'high': 1.0}.get(it.get('register'), np.nan) for it in prim])
    # a song that belongs to one singer only follows that singer (no loss possible)
    song_single = {}
    for g in range(len(songs)):
        ss = set(S[G == g])
        if len(ss) == 1: song_single[g] = ss.pop()
    free_songs = [g for g in range(len(songs)) if g not in song_single]
    target = np.array([0.20, 0.15, 0.65])

    def realise(ls, lg):
        lg = lg.copy()
        for g, s in song_single.items(): lg[g] = ls[s]
        return lg

    DUR = np.array([it['duration_s'] for it in prim])

    def dev2(share):
        d = share - target
        return 400 * (d ** 2).sum() + 4000 * (np.maximum(0, np.abs(d) - 0.05) ** 2).sum()

    def cost(ls, lg):
        lg = realise(ls, lg)
        lab = ls[S]; keep = (lab == lg[G]) & ~(tuned & (lab == 0))
        c = 0.0
        for k in range(len(srcs)):
            m = SRC == k
            c -= 100 * keep[m].mean()
            sk = np.unique(S[m & keep])
            if len(sk) == 0: c += 1e4; continue
            c += dev2(np.bincount(ls[sk], minlength=3) / len(sk))                       # singer shares
            mk = m & keep
            c += dev2(np.bincount(lab[mk], weights=DUR[mk], minlength=3) / DUR[mk].sum())  # minute shares
            for v in (sexv, regv):
                mm = mk & np.isfinite(v)
                if mm.sum() < 5: continue
                allm = v[mm].mean()
                for lab_k in (0, 1):
                    q = mm & (lab == lab_k)
                    c += 30 * (v[q].mean() - allm) ** 2 if q.sum() else 30
        return c

    best = (np.inf, None, None)
    for restart in range(4):
        rng = np.random.default_rng(SEED + restart)
        ls = rng.choice(3, len(singers), p=target); lg = rng.choice(3, len(songs), p=target)
        cur = cost(ls, lg); T = 5.0
        for it in range(30000):
            nls, nlg = ls.copy(), lg.copy()
            if free_songs and rng.random() < 0.5:
                g = free_songs[rng.integers(len(free_songs))]; nlg[g] = rng.integers(3)
            else:
                s = rng.integers(len(singers)); nls[s] = rng.integers(3)
            c = cost(nls, nlg)
            if c < cur or rng.random() < np.exp((cur - c) / T):
                ls, lg, cur = nls, nlg, c
                if c < best[0]: best = (c, ls.copy(), lg.copy())
            T = max(0.02, T * 0.9997)
    _, ls, lg = best; lg = realise(ls, lg)
    names = {0: 'holdout-sealed', 1: 'validation', 2: 'dev'}
    for i, it in enumerate(prim):
        a, b = ls[S[i]], lg[G[i]]
        if a != b:
            it['split'] = 'excluded-crossover'
            it['split_note'] = f'singer in {names[a]}, song in {names[b]}'
        elif a == 0 and tuned[i]:
            it['split'] = 'excluded-prior-tuning'
        else:
            it['split'] = names[a]
    for it in items:
        if it['source'] == 'csd':
            it['split'] = 'dev'
            it['split_note'] = 'secondary set is dev-only: csd_bench has tuned on and repeatedly scored every CSD singer'
    return {'singers': {s: names[ls[i]] for i, s in enumerate(singers)}, 'songs': {s: names[lg[i]] for i, s in enumerate(songs)}}


# ---------------------------------------------------------------- main
def main():
    require_free()
    items = n20em_items() + vocadito_items() + csd_items() + al_items()
    for it in items:
        notes, f0 = it.pop('notes'), it.pop('f0')
        it['notes'] = notes  # temporarily kept for sex inference
        ns = note_stats(notes) if notes else None
        it['note_stats'] = ns
        lic, url, use = LIC[it['source']]
        it.update(licence=lic, licence_url=url, use=use)
        if notes:
            kf = key_fit(notes)
            it['key_truth'] = kf['key'] if kf else None
            it['key_truth_method'] = 'derived-from-notes (duration-weighted Temperley fit)'
            it['key_fit'] = kf
            it['key_ambiguous'] = kf['ambiguous'] if kf else True
            it['key_windows'] = key_segments(notes, it['duration_s'])
            it['modulation_candidates'] = modulation_candidates(it['key_truth'], it['key_windows'])
        else:
            it['key_truth'] = it['label_key']; it['key_truth_method'] = 'label (Apple Loops key signature + key type; unverified)'
            h = np.zeros(12); v = f0[np.isfinite(f0)]
            np.add.at(h, np.round(v).astype(int) % 12, 1.0)
            kf = key_from_hist(h) if len(v) > 50 else None
            it['key_fit_from_f0'] = kf
            it['label_agrees_with_f0_fit'] = (kf is not None and kf['key'] == it['label_key'])
            t, md = it['label_key'].split(); r0 = NAMES.index(t)
            scale = {(r0 + i) % 12 for i in ((0, 2, 3, 5, 7, 8, 10) if md == 'minor' else (0, 2, 4, 5, 7, 9, 11))}
            it['label_in_scale_frac'] = round(float(h[list(scale)].sum() / h.sum()), 3) if h.sum() else None
            it['key_ambiguous'] = None
        med = ns['median_midi_dur_weighted'] if ns else (round(float(np.nanmedian(f0)), 2) if np.isfinite(f0).any() else None)
        it['median_midi'] = med
        it['median_midi_method'] = 'annotated notes (duration-weighted)' if ns else 'reference f0 (voiced frames)'
        it['vibrato'] = vibrato(f0, notes)
        it['ornaments'] = ornaments(f0, notes) if notes else None
        it['pace'] = pace(ns)
        if notes and it['source'] == 'n20emv2':
            # alignment sanity: annotated note vs reference f0 in the note core (octave-folded)
            d = []
            for s, e, m in notes:
                c = f0[int((s + 0.05) / HOP):int((e - 0.03) / HOP)]
                if np.isfinite(c).sum() >= 5:
                    x = (np.nanmedian(c) - m + 6) % 12 - 6; d.append(abs(x))
            it['annotation_vs_f0'] = {'notes_checked': len(d), 'frac_within_50c': round(float(np.mean(np.array(d) <= 0.5)), 3) if d else None}
    sexinf = infer_sex(items)
    for it in items:
        if it['singer_id'] in sexinf:
            it['sex_inferred'], it['singer_median_midi'] = sexinf[it['singer_id']]
            it['sex_inferred_method'] = 'singer-level median annotated MIDI (< 59 M, > 59 F); not a label'
        elif it['source'] == 'csd':
            it['sex_inferred'] = 'F'; it['sex_inferred_method'] = 'voice part (soprano); not a label'
        sx = it.get('sex') or it.get('sex_inferred')
        it['register'] = register(it['median_midi'], sx)
        it['register_basis'] = None if it['register'] is None else ('labelled sex' if it.get('sex') else 'inferred sex')
    arrangement_keys(items)
    split_map = make_split(items)
    for it in items:
        it.pop('notes')
    order = ['id', 'source', 'set', 'licence', 'licence_url', 'use', 'split', 'path', 'singer_id', 'song_id', 'sex', 'sex_inferred',
             'register', 'genre', 'duration_s', 'sample_rate', 'sr_limited', 'key_truth', 'key_truth_method', 'key_ambiguous',
             'notes_available', 'f0_available', 'has_bleed']
    items = [{**{k: it.get(k) for k in order}, **{k: v for k, v in it.items() if k not in order}} for it in items]
    items.sort(key=lambda x: (x['set'], x['source'], x['id']))
    counts = collections.Counter((it['set'], it['split']) for it in items)
    header = {
        'name': 'Sattari lead-vocal corpus (Auto Pitch / Auto Key benchmarking)',
        'version': 1, 'built': datetime.date.today().isoformat(), 'builder': 'validation/leadvocal/build_manifest.py',
        'access_rule': 'Read items ONLY through validation/leadvocal/corpus.py items(). The holdout-sealed split is opened only for '
                       'milestone validation: items("holdout-sealed", allow_holdout=True) with LEADVOCAL_MILESTONE="<reason>" set; '
                       'every opening is appended to validation/leadvocal/holdout-access.log (committed). Nothing is tuned on it.',
        'split_rule': ('Split by SINGER and SONG jointly (seed %d). Every singer and every song (canonical song_id, shared across '
                       'sources: e.g. love-me-tender, twinkle-twinkle tune) gets exactly one label in {holdout-sealed, validation, '
                       'dev}; an item is kept only when its singer label equals its song label, otherwise it is '
                       'excluded-crossover. Consequently no singer and no song appears in two splits. Items that earlier '
                       'benchmarks tuned on (al_vocals dev, vocadito_bench dev) may not enter the sealed holdout '
                       '(excluded-prior-tuning). Labels are chosen by simulated annealing that maximises the kept fraction per '
                       'source, targets singer shares and kept-minute shares of holdout 20%% / validation 15%% / dev 65%% per source (4 restarts, seeds SEED..SEED+3), and balances '
                       'sex (labelled or inferred) and register between splits. Secondary Apple Loops items (harmony / background parts) follow their singer and song labels; the CSD sopranos are dev-only.') % SEED,
        'split_seed': SEED,
        'label_policy': 'Fields that cannot be derived are null (unknown). sex is null unless labelled by the source; '
                        'sex_inferred is a pitch-based inference used only for balancing and coverage reporting.',
        'register_rule': 'median MIDI vs sex: M low < 50 (D3) <= mid <= 57 (A3) < high; F low < 60 (C4) <= mid <= 67 (G4) < high',
        'key_rule': f'duration-weighted Temperley (1999) profile correlation over note annotations; margin = r(best) - r(runner-up); '
                    f'key_ambiguous when margin < {AMBIGUOUS_MARGIN}. key_windows: 30 s windows / 10 s hop, a segment needs >= 3 '
                    f'consecutive confident windows (diagnostic). modulation_candidates: same-mode segments >= 50 s whose tonic is '
                    f'1-2 st from the global key. N20EM: performances of one song are grouped by pitch-class histogram (r >= 0.85) into '
                    f'arrangement groups (same backing-track key) and key_truth is the fit of the POOLED notes of the group '
                    f'(key_fit_item keeps the single-performance fit); modulation_confirmed = candidates shared by >= half of a group. Apple Loops keys are labels, cross-checked by a '
                    f'Temperley fit of the reference f0 (label_agrees_with_f0_fit).',
        'counts': {f'{a}/{b}': n for (a, b), n in sorted(counts.items())},
        'split_labels': split_map,
    }
    json.dump({'header': header, 'items': items}, open(HERE / 'manifest.json', 'w'), indent=1, default=float)
    print(json.dumps(header['counts'], indent=1))


if __name__ == '__main__':
    main()
