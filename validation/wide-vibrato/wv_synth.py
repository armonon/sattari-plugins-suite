"""Wide-vibrato controlled benchmark (owner spec 2026-09-27: wide vibrato is the first perceptual project).

A vocal-like harmonic tone with sinusoidal vibrato (depth = half peak-to-peak, cents; rate, Hz) around a
centre that sits `offset` cents above a semitone, rendered through AutoPitchBench (C chromatic,
Amount 1, Speed 1). Measured on the steady part (0.6 s .. end-0.2 s, latency compensated):
Pitch is CREPE-full (validation/accuracy_20260923/pitchref.py), the scorer of the real-singer metrics.
A Hilbert instantaneous frequency of the fundamental was tried first and rejected: Rubber Band smears
the fundamental of a fast, wide pitch curve and biased it by up to 11 c against CREPE (2026-09-29).
  centre   mean output pitch minus the semitone nearest the INPUT centre (c)
  depth    p90 |output - its mean| (c); ratio = output depth / input depth
  asym     |upper peak| - |lower peak| of the output around its mean (c)
  flips    changes per second of the plugin's chosen target (trace targetMidi, after hysteresis)
usage: wv_synth.py [tag] [bench args...]  (e.g. `vt --vibrato-target`)   BENCH=<AutoPitchBench> (default: the R&D build)
"""
import json, os, subprocess, sys, itertools
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np, soundfile as sf
from scipy.signal import butter, sosfiltfilt, hilbert, medfilt
sys.path.insert(0, str(Path(__file__).parents[1] / 'accuracy_20260923'))
if not (Path(__file__).parents[1] / 'accuracy_20260923').exists():
    sys.path.insert(0, '/Users/agentsattari/code/sattari-plugins-suite-live-integration/validation/accuracy_20260923')

from pitchref import f0                                # noqa: E402
SR = 48000
PITCH = {}
HERE = Path(__file__).parent; WORK = HERE / 'work'; WORK.mkdir(exist_ok=True)
B = os.environ.get('BENCH', str(HERE.parents[1] / 'build/auto-pitch-rnd/AutoPitchBench'))
BASE = 57                                              # A3

def tone(offset, depth, rate, secs=2.6, seed=1):
    t = np.arange(int(secs * SR)) / SR
    midi = BASE + offset / 100 + depth / 100 * np.sin(2 * np.pi * rate * t)
    ph = 2 * np.pi * np.cumsum(440 * 2 ** ((midi - 69) / 12)) / SR
    y = sum((0.6 / k) * np.sin(k * ph) for k in range(1, 9))
    env = np.minimum(1, t / 0.05) * np.minimum(1, (secs - t) / 0.05)
    return (0.25 * env * y + 0.002 * np.random.default_rng(seed).standard_normal(len(t))).astype(np.float32)

def inst_midi(y, centre_midi):
    f0 = 440 * 2 ** ((centre_midi - 69) / 12)
    sos = butter(4, [f0 * 2 ** (-2.6 / 12), f0 * 2 ** (2.6 / 12)], 'bandpass', fs=SR, output='sos')
    a = hilbert(sosfiltfilt(sos, y)); f = np.diff(np.unwrap(np.angle(a))) * SR / (2 * np.pi)
    f = medfilt(f, 241)[::480]                          # 10 ms frames
    return 69 + 12 * np.log2(np.maximum(f, 1.0) / 440)

def one(args):
    (offset, depth, rate, style), extra, tag = args
    name = f"{tag}_{style}_o{offset}_d{depth}_r{rate}"
    src = WORK / f"in_o{offset}_d{depth}_r{rate}.wav"     # written before the pool starts
    out, tr = WORK / f"{name}.wav", WORK / f"{name}.json"
    out.unlink(missing_ok=True)
    subprocess.run([B, str(src), '--output', str(out), '--trace', str(tr), '--key', 'C', '--scale', 'chromatic',
                    '--amount', '1', '--speed', '1', '--block', '512', '--style', style, *extra],
                   check=True, capture_output=True, env=dict(os.environ, SATTARI_SESSION_ID=f"wv-{name}"))
    return name, src, out, tr

def score(args, name, src, out, tr):
    (offset, depth, rate, style), extra, tag = args
    t = json.load(open(tr)); lat = t['reportedLatencySamples']
    y, _ = sf.read(out, dtype='float32'); y = (y if y.ndim == 1 else y[:, 0])[lat:]
    PITCH[name] = f0(y)[0]
    if 'in_' + src.stem not in PITCH:
        PITCH['in_' + src.stem] = f0(sf.read(src, dtype='float32')[0])[0]
    c_in = BASE + offset / 100; nearest = round(c_in)
    m, mi = PITCH[name], PITCH['in_' + src.stem]
    n = min(len(m), len(mi)); m, mi = m[:n], mi[:n]
    a, b = 60, n - 20
    ok = np.isfinite(m[a:b]) & np.isfinite(mi[a:b])
    seg = 100 * (m[a:b][ok] - nearest); segi = 100 * (mi[a:b][ok] - nearest)
    dev = seg - seg.mean()
    depth_out = float(np.percentile(np.abs(dev), 90)); depth_in = float(np.percentile(np.abs(segi - segi.mean()), 90))
    fr = [f for f in t['frames'] if f['voiced'] and 0.6 <= f['timeSec'] <= 2.4]
    tg = np.array([round(f['targetMidi']) for f in fr]); flips = int(np.sum(tg[1:] != tg[:-1])) / 1.8
    out.unlink(); tr.unlink()
    return dict(style=style, offset=offset, depth=depth, rate=rate, centre=float(seg.mean()), depth_out=depth_out,
                depth_in=depth_in, ratio=depth_out / max(depth_in, 1e-6),
                asym=float(np.percentile(dev, 97) + np.percentile(dev, 3)), flips=flips)

if __name__ == '__main__':
    tag = sys.argv[1] if len(sys.argv) > 1 else 'base'; extra = sys.argv[2:]
    grid = list(itertools.product((0, 20, 35, 45), (30, 50, 70, 100), (5.0, 6.5), ('Natural', 'Classic')))
    for (o, d, r) in {g[:3] for g in grid}:
        src = WORK / f"in_o{o}_d{d}_r{r}.wav"
        if not src.exists():
            sf.write(src, tone(o, d, r), SR, subtype='FLOAT')
    jobs = [(g, extra, tag) for g in grid]
    with ThreadPoolExecutor(8) as ex:
        rendered = list(ex.map(one, jobs))
    rows = [score(j, *r) for j, r in zip(jobs, rendered)]
    json.dump(rows, open(WORK / f"wv_{tag}.json", 'w'), indent=1)
    for style in ('Natural', 'Classic'):
        print(f"== {tag} {style}: rows offset c / depth c -> ratio, |centre| c, flips/s  (mean over rates 5 and 6.5 Hz)")
        for o in (0, 20, 35, 45):
            cells = []
            for d in (30, 50, 70, 100):
                r = [x for x in rows if x['style'] == style and x['offset'] == o and x['depth'] == d]
                cells.append(f"{np.mean([x['ratio'] for x in r]):4.2f} {np.mean([abs(x['centre']) for x in r]):4.0f}c {np.mean([x['flips'] for x in r]):4.1f}")
            print(f"  off {o:2d}: " + " | ".join(cells))
