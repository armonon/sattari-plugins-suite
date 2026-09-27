"""Objective Auto Pitch metrics on real singers with note annotations (owner spec 2026-09-27, Part I s3/6/7/8).

Everything is reported per SEGMENT, never only as one average:
  register (median note MIDI: low < 55 <= mid < 67 <= high), note length (<150 ms, 150-400, >400),
  vibrato depth of the input note (<15 c, 15-40, >40 c), transition interval into the note
  (unison/semitone/whole/third/fourth-fifth/sixth+/octave+; legato = gap < 60 ms), direction.

Per note (target = nearest semitone of the annotated pitch; notes both annotators agree on when two
annotations exist), from CREPE-tiny f0 of input and output (the scorer is the same model for both):
  core error    mean output pitch over the note core (onset+80 ms .. end-50 ms) minus target
  within 5/10/20 c, median |err|, p95 |err|
  target_ok     output core rounds to the target semitone
  octave_err    |err| > 600 c
  false_corr    input core already within 10 c of target but output worse than 20 c
  flips         the 200 ms-smoothed output core changes semitone for >= 30 ms (unstable target;
                smoothing removes preserved vibrato)
  settle_ms     legato notes only: time from annotated onset until the output stays within 20 c of
                the target for 50 ms (capped at 400 ms)
  vibrato       notes with input depth > 25 c at 4-8 Hz: output/input depth and rate ratios
Onset / consonant timing (s6): strong broadband onsets in the input (spectral-flux peaks above the
99th percentile, >= 150 ms apart) are matched in the output; reported: onset shift (ms, output minus
input, after the reported latency), attack attenuation (dB, 2-8 kHz energy in the first 30 ms), and
onset sharpness (10-90% rise time of the 2-8 kHz envelope, ms).

usage: pitch_metrics.py <tag> <set: vocadito|n20em> <split> [--shard i/n] [bench args...]
       pitch_metrics.py merge <tag> <set> <split>        (combine shards and report)
       BENCH=<AutoPitchBench>  (default: the FROZEN candidate build, read-only)
"""
import csv, json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np, soundfile as sf
sys.path.insert(0, str(Path(__file__).parents[1] / 'accuracy_20260923'))
from pitchref import load, f0, SR          # noqa: E402
from diskguard import require_free         # noqa: E402

HERE = Path(__file__).parent
WORK = HERE / 'work'; WORK.mkdir(exist_ok=True)
FROZEN = Path('/Users/agentsattari/code/sattari-plugins-suite-live-integration/build/auto-pitch-neural/AutoPitchBench')
B = Path(os.environ.get('BENCH', str(FROZEN)))
midi = lambda hz: 69 + 12 * np.log2(np.asarray(hz, float) / 440.0)

# ---------------------------------------------------------------------------- corpora
VOC = Path.home() / 'datasets/vocadito'

def vocadito_items(split):
    meta = {r['track_id']: r for r in csv.DictReader(open(VOC / 'vocadito_metadata.csv'))}
    singers = sorted({r['singer_id'] for r in meta.values()}, key=lambda s: int(s[1:]))
    hold = set(singers[2::3])                                  # same split as vocadito_bench.py
    out = []
    for t in sorted(meta, key=int):
        if split != 'all' and (meta[t]['singer_id'] in hold) != (split == 'holdout'):
            continue
        def notes(a):
            return [(float(s), float(s) + float(d), float(midi(float(f))))
                    for s, f, d in csv.reader(open(VOC / f'Annotations/Notes/vocadito_{t}_notes{a}.csv'))]
        a1, a2 = notes('A1'), notes('A2'); agreed = []
        for s, e, m in a1:
            mid = (s + e) / 2; o = [n for n in a2 if n[0] <= mid <= n[1]]
            if o and round(o[0][2]) == round(m):
                agreed.append((s, e, int(round(m))))
        out.append({'id': f'vocadito_{t}', 'wav': str(VOC / f'Audio/vocadito_{t}.wav'), 'notes': agreed,
                    'singer': meta[t]['singer_id']})
    return out

def leadvocal_items(split, source='n20emv2'):
    """Lead-vocal corpus via the sealed loader (validation/leadvocal/corpus.py). N20EM notes come from
    its expert annotations; holdout-sealed is refused unless the loader's milestone rules are met."""
    sys.path.insert(0, str(Path(__file__).parents[1] / 'leadvocal'))
    import corpus
    ann = json.load(open(Path.home() / 'datasets/n20emv2/annotations.json'))
    out = []
    for it in corpus.items(split, allow_holdout=(split == 'holdout-sealed'), source=source, set='primary'):
        key = it['id'].split(':', 1)[1]
        notes = [(float(s), float(e), int(round(m))) for s, e, m in ann[key]['midi'] if e - s > 0.0]
        out.append({'id': it['id'].replace(':', '_'), 'wav': os.path.expanduser(it['path']), 'notes': notes,
                    'singer': it['singer_id'], 'sex': it.get('sex') or it.get('sex_inferred'), 'genre': it.get('genre')})
    return out

SETS = {'vocadito': vocadito_items, 'n20em': leadvocal_items}

# ---------------------------------------------------------------------------- analysis
def ref(path, lat=0):
    c = Path(str(path) + f'.lat{lat}.crepetiny.s0.npy')
    if c.exists():
        return np.load(c)
    y, sr = sf.read(str(path), dtype='float32', always_2d=False)
    y = (y if y.ndim == 1 else y.mean(1))[lat:]
    if sr != SR:                                   # native-rate render: resample for scoring only
        import librosa
        y = librosa.resample(y, orig_sr=sr, target_sr=SR)
    m, _ = f0(y, model='tiny'); np.save(c, m); return m

def core(t, m, s, e):
    sel = (t >= s + 0.08) & (t <= e - 0.05) & np.isfinite(m)
    return m[sel] if sel.sum() >= 5 else None

def vibrato(seg):
    """depth (c, half peak-to-peak of the detrended contour) and rate (Hz) of a 100 Hz f0 segment."""
    if seg is None or len(seg) < 30:
        return 0.0, 0.0
    x = 100 * (seg - np.polyval(np.polyfit(np.arange(len(seg)), seg, 1), np.arange(len(seg))))
    depth = float(np.percentile(np.abs(x), 90))
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)), 1024)); f = np.fft.rfftfreq(1024, 0.01)
    band = (f >= 3) & (f <= 9)
    return depth, float(f[band][np.argmax(X[band])])

def hf_env(y, sr):
    from scipy.signal import butter, sosfiltfilt
    sos = butter(4, [2000, 8000], 'bandpass', fs=sr, output='sos')
    h = sosfiltfilt(sos, y); win = int(0.002 * sr)
    return np.sqrt(np.convolve(h * h, np.ones(win) / win, 'same') + 1e-12)

def onsets(y, sr):
    hop = int(0.005 * sr); n = 1024
    frames = np.lib.stride_tricks.sliding_window_view(np.pad(y, (0, n)), n)[::hop] * np.hanning(n)
    S = np.abs(np.fft.rfft(frames, axis=1)); fl = np.maximum(0, np.diff(np.log1p(S), axis=0)).sum(1)
    thr = np.percentile(fl, 99); idx = []
    for i in np.argsort(-fl):
        if fl[i] < thr: break
        if all(abs(i - j) * hop >= 0.15 * sr for j in idx): idx.append(i)
    return sorted((i + 1) * hop for i in idx)

def seg_register(m):  return 'low' if m < 55 else ('mid' if m < 67 else 'high')
def seg_len(d):       return '<150ms' if d < 0.15 else ('150-400ms' if d < 0.4 else '>400ms')
def seg_vib(dep):     return '<15c' if dep < 15 else ('15-40c' if dep < 40 else '>40c')
def seg_interval(k):
    k = abs(k); return ['unison', 'semitone', 'whole', 'third', 'third', 'fourth-fifth', 'fourth-fifth',
                        'fourth-fifth', 'sixth+', 'sixth+', 'sixth+', 'sixth+'][k] if k < 12 else 'octave+'

# ---------------------------------------------------------------------------- run
def run(tag, set_name, split, extra, shard=None):
    require_free()
    items = SETS[set_name](split)
    if shard:
        i, n = shard; items = items[i::n]
    native = os.environ.get('PM_NATIVE_SR') == '1'     # render at the file's own rate (e.g. 44.1 kHz)
    def render(it):
        src = WORK / (f"{it['id']}_native.wav" if native else f"{it['id']}.wav")
        if not src.exists():
            x, sr = sf.read(it['wav']); x = x if x.ndim == 1 else x.mean(1)
            if native:
                sf.write(src, x, sr, subtype='FLOAT')
            elif sr != SR:
                subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', it['wav'], '-ac', '1', '-ar', str(SR), str(src)], check=True)
            else:
                sf.write(src, x, SR)
        out = WORK / f"{it['id']}_{tag}.wav"; tr = WORK / f"{it['id']}_{tag}.json"
        subprocess.run([str(B), str(src), '--output', str(out), '--trace', str(tr), '--key', 'C', '--scale', 'chromatic',
                        '--amount', '1', '--speed', '1', '--block', '512', *(extra or ['--style', 'Classic'])],
                       check=True, capture_output=True, env=dict(os.environ, SATTARI_SESSION_ID=f"pm-{tag}-{it['id']}"))
        return it['id'], json.load(open(tr))['reportedLatencySamples']
    with ThreadPoolExecutor(8) as ex:
        lats = dict(ex.map(render, items))

    rows, onset_rows = [], []
    for it in items:
        src = WORK / (f"{it['id']}_native.wav" if native else f"{it['id']}.wav"); out = WORK / f"{it['id']}_{tag}.wav"; lat = lats[it['id']]
        mi, mo = ref(src), ref(out, lat); ti = np.arange(len(mi)) * 0.01; to = np.arange(len(mo)) * 0.01
        notes = it['notes']
        for k, (s, e, tgt) in enumerate(notes):
            ci, co = core(ti, mi, s, e), core(to, mo, s, e)
            if ci is None or co is None:
                continue
            ein, eout = 100 * (np.mean(ci) - tgt), 100 * (np.mean(co) - tgt)
            if abs(ein) > 48:
                continue                                    # intended note ambiguous from the input
            # smooth over ~one vibrato period first, so a preserved wide vibrato is not a 'flip'
            k20 = min(len(co), 21); sm = np.convolve(co, np.ones(k20) / k20, 'valid') if len(co) >= k20 else co
            semis = np.round(sm - tgt).astype(int); runs, cur, n = [], semis[0], 0
            for v in semis:
                if v == cur: n += 1
                else: runs.append((cur, n)); cur, n = v, 1
            runs.append((cur, n)); flips = max(0, sum(1 for _, l in runs if l >= 3) - 1)
            prev = notes[k - 1] if k else None
            legato = prev is not None and s - prev[1] < 0.06
            interval = seg_interval(tgt - prev[2]) if legato else 'detached'
            settle = None
            if legato:
                sel = (to >= s) & (to <= s + 0.45); seg = 100 * (mo[sel] - tgt); ok = np.abs(seg) <= 20
                for i in range(len(ok) - 5):
                    if ok[i:i + 5].all(): settle = 10.0 * i; break
                settle = 400.0 if settle is None else min(settle, 400.0)
            dep_in, rate_in = vibrato(ci); dep_out, rate_out = vibrato(co)
            rows.append({'item': it['id'], 'sex': it.get('sex') or '-', 'genre': it.get('genre') or '-', 'err': eout, 'err_in': ein, 'tgt': tgt, 'len': e - s,
                         'register': seg_register(tgt), 'length': seg_len(e - s), 'vibrato': seg_vib(dep_in),
                         'interval': interval, 'direction': ('up' if legato and tgt > prev[2] else 'down' if legato and tgt < prev[2] else '-'),
                         'target_ok': abs(eout) < 50, 'octave': abs(eout) > 600, 'false_corr': abs(ein) <= 10 and abs(eout) > 20,
                         'flips': flips, 'settle': settle,
                         'vib_depth_ratio': dep_out / dep_in if dep_in > 25 and 4 <= rate_in <= 8 else None,
                         'vib_rate_ratio': rate_out / rate_in if dep_in > 25 and 4 <= rate_in <= 8 else None})
        # onset / consonant timing on the full signal
        x, fsr = sf.read(src); y, _ = sf.read(out); y = y if y.ndim == 1 else y[:, 0]; y = y[lat:]
        x = x if x.ndim == 1 else x[:, 0]
        n = min(len(x), len(y)); x, y = x[:n], y[:n]; hx, hy = hf_env(x, fsr), hf_env(y, fsr)
        SRl = fsr
        for o in onsets(x, fsr):
            a, b = o - int(0.02 * SRl), o + int(0.06 * SRl)
            if a < 0 or b > n: continue
            ex, ey = hx[a:b], hy[a:b]
            c = np.correlate(ey - ey.mean(), ex - ex.mean(), 'full'); lagi = int(np.argmax(c)) - (len(ex) - 1)
            w = int(0.03 * SRl); pk_in = ex[int(0.02 * SRl):int(0.02 * SRl) + w].max(); pk_out = ey[int(0.02 * SRl):int(0.02 * SRl) + w].max()
            def rise(env):
                seg = env[int(0.01 * SRl):int(0.02 * SRl) + w]; lo, hi = seg.min(), seg.max()
                i10 = np.argmax(seg >= lo + 0.1 * (hi - lo)); i90 = np.argmax(seg >= lo + 0.9 * (hi - lo)); return 1000 * (i90 - i10) / SRl
            onset_rows.append({'shift_ms': 1000 * lagi / SRl, 'atten_db': 20 * np.log10(pk_out / pk_in), 'rise_in': rise(ex), 'rise_out': rise(ey)})
        if os.environ.get('PM_KEEP') != '1':
            out.unlink(missing_ok=True)

    part = f'_part{shard[0]}of{shard[1]}' if shard else ''
    json.dump({'rows': rows, 'onsets': onset_rows}, open(WORK / f'metrics_{tag}_{set_name}_{split}{part}.json', 'w'), default=lambda v: v.item() if hasattr(v, 'item') else str(v))
    if not shard:
        report(tag, set_name, split, rows, onset_rows)

def merge(tag, set_name, split):
    rows, ons = [], []
    for f in sorted(WORK.glob(f'metrics_{tag}_{set_name}_{split}_part*.json')):
        d = json.load(open(f)); rows += d['rows']; ons += d['onsets']
    report(tag, set_name, split, rows, ons)

def summary(rs):
    a = np.abs([r['err'] for r in rs]); settle = [r['settle'] for r in rs if r['settle'] is not None]
    return (f"n={len(rs):4d}  <=5c {100*np.mean(a<=5):5.1f}  <=10c {100*np.mean(a<=10):5.1f}  <=20c {100*np.mean(a<=20):5.1f}  "
            f"med {np.median(a):5.1f}c  p95 {np.percentile(a,95):5.1f}c  target {100*np.mean([r['target_ok'] for r in rs]):5.1f}%  "
            f"oct {sum(r['octave'] for r in rs)}  falsecorr {sum(r['false_corr'] for r in rs)}  "
            f"flips>0 {100*np.mean([r['flips']>0 for r in rs]):4.1f}%" + (f"  settle med {np.median(settle):4.0f}ms p90 {np.percentile(settle,90):4.0f}ms" if settle else ''))

def report(tag, set_name, split, rows, onset_rows):
    print(f'== {tag} on {set_name}/{split} ==')
    print('ALL            ', summary(rows))
    for key in ('sex', 'genre', 'register', 'length', 'vibrato', 'interval', 'direction'):
        for v in sorted({r.get(key, '-') for r in rows}):
            if key in ('sex', 'genre') and v == '-': continue
            rs = [r for r in rows if r.get(key, '-') == v]
            if len(rs) >= 10:
                print(f'{key[:8]:8s} {v:12s}', summary(rs))
    vd = [r['vib_depth_ratio'] for r in rows if r['vib_depth_ratio'] is not None]
    vr = [r['vib_rate_ratio'] for r in rows if r['vib_rate_ratio'] is not None]
    if vd:
        print(f'vibrato notes n={len(vd)}: depth out/in median {np.median(vd):.2f} (p10 {np.percentile(vd,10):.2f}, p90 {np.percentile(vd,90):.2f}); rate out/in median {np.median(vr):.2f}')
    if onset_rows:
        s = np.array([o['shift_ms'] for o in onset_rows]); at = np.array([o['atten_db'] for o in onset_rows])
        ri = np.array([o['rise_in'] for o in onset_rows]); ro = np.array([o['rise_out'] for o in onset_rows])
        print(f'onsets n={len(s)}: shift median {np.median(s):+.1f} ms (p10 {np.percentile(s,10):+.1f}, p90 {np.percentile(s,90):+.1f}); '
              f'2-8 kHz attack {np.median(at):+.1f} dB median; rise time in {np.median(ri):.1f} ms -> out {np.median(ro):.1f} ms')

if __name__ == '__main__':
    if sys.argv[1] == 'merge':
        merge(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        a = sys.argv[1:]; shard = None
        if '--shard' in a:
            k = a.index('--shard'); i, n = a[k + 1].split('/'); shard = (int(i), int(n)); del a[k:k + 2]
        run(a[0], a[1], a[2], a[3:], shard)
