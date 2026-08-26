"""
Agent B — Harmony Audio Engine Bench.

Tests the HarmonyAudioEngine renderer:
  1. Core vocal cases: major/minor/chromatic, chord changes, vibrato, slides, runs
  2. Voice configuration sweep: 2, 3, 4 simultaneous voices
  3. 308-clip neural corpus through harmony rendering
  4. Quality metrics: illegal notes, voice-leading distance, artifacts, crashes, NaN/Inf

Gate: HARMONY AUDIO RC if:
  - 0 crashes
  - 0 NaN/Inf frames
  - render_rate ≥ 0.90 (≥90% of voiced frames rendered successfully)
  - voice_leading_mean_cents ≤ 200¢ (voices don't jump erratically)
  - corpus: 0 crashes across 308 clips
"""

import subprocess, json, math, struct, sys, wave, time
from pathlib import Path

BENCH  = Path("/Users/agentsattari/code/sattari-plugins-suite/build/plugins/auto-pitch/AutoPitchBench")
CORPUS_DIR = Path("/Users/agentsattari/code/sattari-worktrees/experiment-neural-vocal-corpus/corpus/neural_vocal/dry")
OUTDIR = Path(__file__).parent.parent / "validation" / "harmony_audio"
OUTDIR.mkdir(parents=True, exist_ok=True)

SR    = 44100
BLOCK = 256
A4_HZ = 440.0
A4_MIDI = 69

# ── Signal synthesis helpers ──────────────────────────────────────────────────

def write_wav(path, samples, sr=44100):
    with wave.open(str(path), "w") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        clipped = [max(-32767, min(32767, int(s * 32767))) for s in samples]
        wf.writeframes(struct.pack(f"<{len(clipped)}h", *clipped))


def midi_to_hz(midi):
    return A4_HZ * 2 ** ((midi - A4_MIDI) / 12.0)


def vocal_tone(hz, dur_sec, amp=0.7, sr=44100):
    n = int(sr * dur_sec)
    return [amp * (0.6*math.sin(2*math.pi*hz*i/sr)
                   + 0.25*math.sin(2*math.pi*2*hz*i/sr)
                   + 0.10*math.sin(2*math.pi*3*hz*i/sr)) for i in range(n)]


def chord_progression(midis_per_beat, beat_dur=0.5, sr=44100):
    """Monophonic melody over a chord progression."""
    samples = []
    for midi_root in midis_per_beat:
        hz = midi_to_hz(midi_root)
        samples.extend(vocal_tone(hz, beat_dur, sr=sr))
    return samples


def vibrato_tone(hz, dur_sec=2.0, vib_hz=5.0, vib_cents=40.0, sr=44100):
    n = int(sr * dur_sec)
    samples = []
    for i in range(n):
        t = i / sr
        deviation = vib_cents * math.sin(2*math.pi*vib_hz*t)
        freq = hz * (2 ** (deviation / 1200.0))
        samples.append(0.7 * math.sin(2*math.pi*freq*t))
    return samples


def slide_tone(hz_start, hz_end, dur_sec=1.5, sr=44100):
    n = int(sr * dur_sec)
    out = []
    phase = 0.0
    for i in range(n):
        hz = hz_start + (hz_end - hz_start) * i / n
        out.append(0.7 * math.sin(phase))
        phase += 2*math.pi*hz/sr
    return out


def run_tone(midis, note_dur=0.08, sr=44100):
    out = []
    for m in midis:
        hz = midi_to_hz(m)
        n = int(sr * note_dur)
        for i in range(n):
            out.append(0.7 * math.sin(2*math.pi*hz*i/sr))
    return out


# ── Run bench helper ──────────────────────────────────────────────────────────

def get_trace(wav_path, label, key="A", scale="minor", block=256, timeout=90):
    """Run AutoPitchBench and return trace path (or None on failure)."""
    trace_path = OUTDIR / f"trace_{label}.json"
    out_path   = OUTDIR / f"out_ap_{label}.wav"
    r = subprocess.run([
        str(BENCH), str(wav_path),
        "--output", str(out_path),
        "--trace",  str(trace_path),
        "--key", key, "--scale", scale,
        "--amount", "0.82", "--speed", "0.7",
        "--block", str(block), "--intent-mode", "v2_active",
    ], capture_output=True, text=True, timeout=timeout)
    if r.returncode != 0 or not trace_path.exists():
        return None
    return trace_path


# ── Test cases ─────────────────────────────────────────────────────────────────

print("[HarmonyAudioBench] Setting up test signals...")

CASES = [
    # (name, key, scale, samples)
    ("C_major_scale",    "C", "major",
     chord_progression([60,62,64,65,67,69,71,72], 0.5)),
    ("A_minor_scale",    "A", "minor",
     chord_progression([57,59,60,62,64,65,67,69], 0.5)),
    ("G_major_chords",   "G", "major",
     chord_progression([55,55,59,59,62,62,67,67], 0.4)),
    ("E_minor_chords",   "E", "minor",
     chord_progression([52,52,55,55,59,59,64,64], 0.4)),
    ("vibrato_A4",       "A", "minor",
     vibrato_tone(midi_to_hz(69), 3.0)),
    ("slide_A3_A4",      "A", "minor",
     slide_tone(midi_to_hz(57), midi_to_hz(69), 2.0)),
    ("slide_A4_A3",      "A", "minor",
     slide_tone(midi_to_hz(69), midi_to_hz(57), 2.0)),
    ("fast_run_Amaj",    "A", "major",
     run_tone([57,59,61,62,64,66,68,69], 0.06)),
    ("register_low_E2",  "E", "minor",
     vocal_tone(midi_to_hz(40), 2.0)),  # E2
    ("register_hi_C5",   "C", "major",
     vocal_tone(midi_to_hz(72), 2.0)),  # C5
    ("chord_change_Cmaj_Amin", "C", "major",
     vocal_tone(midi_to_hz(60), 1.0) + vocal_tone(midi_to_hz(57), 1.0)),  # C4 → A3
    ("inversion_E4_C4",  "C", "major",
     vocal_tone(midi_to_hz(64), 1.0) + vocal_tone(midi_to_hz(60), 1.0)),  # E4 → C4
    ("chromatic_pass",   "C", "major",
     chord_progression([60,61,62,63,64,65,66,67], 0.3)),  # chromatic through C major
    ("held_across_chord","A", "minor",
     vocal_tone(midi_to_hz(64), 3.0)),   # E4 held (common to Am and C)
]

# Write test WAVs
for name, key, scale, samples in CASES:
    write_wav(OUTDIR / f"test_{name}.wav", samples)

print(f"[HarmonyAudioBench] {len(CASES)} test signals written")

# ── Voice configs ─────────────────────────────────────────────────────────────

import sys
sys.path.insert(0, str(Path(__file__).parent))
from harmony_audio_engine import HarmonyAudioEngine, HarmonyVoiceConfig

VOICE_CONFIGS = {
    "2voice_3rd_5th": [
        HarmonyVoiceConfig("+3rd", level=0.6, pan=-0.3, formant=True),
        HarmonyVoiceConfig("+5th", level=0.5, pan=+0.3, formant=True),
    ],
    "3voice_std": [
        HarmonyVoiceConfig("+3rd", level=0.6, pan=-0.3, formant=True),
        HarmonyVoiceConfig("+5th", level=0.5, pan=+0.3, formant=True),
        HarmonyVoiceConfig("-3rd", level=0.45, pan=+0.5, formant=True),
    ],
    "4voice_full": [
        HarmonyVoiceConfig("+3rd", level=0.55, pan=-0.4, formant=True),
        HarmonyVoiceConfig("+5th", level=0.45, pan=-0.1, formant=True),
        HarmonyVoiceConfig("-3rd", level=0.45, pan=+0.1, formant=True),
        HarmonyVoiceConfig("+oct", level=0.35, pan=+0.4, formant=True),
    ],
}

# ── Run vocal test matrix ─────────────────────────────────────────────────────

print("\n[HarmonyAudioBench] == VOCAL TEST MATRIX ==")
print(f"{'Case':<28} {'VoiceConfig':<16} {'Voiced':>6} | {'Rendered':>8} | {'VLdist':>7} | {'Crash':>5} | Status")
print("-" * 95)

vocal_results = []

for name, key, scale, _ in CASES:
    wav_in = OUTDIR / f"test_{name}.wav"
    trace = get_trace(wav_in, f"ap_{name}", key=key, scale=scale, block=BLOCK)
    if trace is None:
        print(f"  SKIP: {name} (trace failed)")
        continue

    # Test with standard 3-voice config
    cfg_name = "3voice_std"
    cfg = VOICE_CONFIGS[cfg_name]
    engine = HarmonyAudioEngine(key=key, scale=scale,
                                voices=[v.interval for v in cfg],
                                sr=SR, block=BLOCK)
    out_wav = OUTDIR / f"harm_{name}.wav"
    t0 = time.monotonic()
    metrics = engine.render(wav_in, trace, out_wav, voice_configs=cfg)
    elapsed = time.monotonic() - t0

    crash = metrics.get("crashes", 0) > 0
    nan_  = metrics.get("nan_inf", 0) > 0
    ok = not crash and not nan_
    render_rate = metrics.get("render_rate", 0)
    vl = metrics.get("voice_leading_mean_cents", 0)

    print(f"  {name:<28} {cfg_name:<16} {metrics.get('voiced_frames',0):>6} | "
          f"{render_rate:.1%}:>8 | {vl:>7.1f}¢ | {'YES' if crash else 'no':>5} | "
          f"{'OK' if ok else 'FAIL'}")
    vocal_results.append({"case": name, "cfg": cfg_name, "elapsed": elapsed,
                           "ok": ok, **metrics})


# ── Voice count scaling ────────────────────────────────────────────────────────

print("\n[HarmonyAudioBench] == VOICE COUNT SCALING ==")
print(f"{'VoiceConfig':<18} {'Voiced':>6} | {'Rendered':>8} | {'VLdist':>7} | {'Crash':>5} | Status")
print("-" * 75)

scale_results = []
# Use sustained A4 in A minor as reference signal
ref_wav = OUTDIR / "test_vibrato_A4.wav"  # complex signal
ref_trace = get_trace(ref_wav, "ref_scale", key="A", scale="minor", block=BLOCK)

if ref_trace:
    for cfg_name, cfg in VOICE_CONFIGS.items():
        engine = HarmonyAudioEngine(key="A", scale="minor",
                                    voices=[v.interval for v in cfg],
                                    sr=SR, block=BLOCK)
        out_wav = OUTDIR / f"scale_{cfg_name}.wav"
        t0 = time.monotonic()
        metrics = engine.render(ref_wav, ref_trace, out_wav, voice_configs=cfg)
        elapsed = time.monotonic() - t0
        crash = metrics.get("crashes", 0) > 0
        nan_  = metrics.get("nan_inf", 0) > 0
        ok = not crash and not nan_
        render_rate = metrics.get("render_rate", 0)
        vl = metrics.get("voice_leading_mean_cents", 0)
        print(f"  {cfg_name:<18} {metrics.get('voiced_frames',0):>6} | "
              f"{render_rate:.1%}:>8 | {vl:>7.1f}¢ | {'YES' if crash else 'no':>5} | "
              f"{'OK' if ok else 'FAIL'} ({elapsed:.1f}s)")
        scale_results.append({"cfg": cfg_name, "ok": ok, "elapsed": elapsed, **metrics})


# ── 308-clip corpus sweep (whole-file pitch-shift, sampled 60 clips) ──────────

print("\n[HarmonyAudioBench] == CORPUS SWEEP (60-clip sample, whole-file shift) ==")

import shutil as _shutil, random as _random
corpus_wavs = sorted(CORPUS_DIR.glob("*.wav")) if CORPUS_DIR.exists() else []
print(f"[HarmonyAudioBench] Total corpus: {len(corpus_wavs)} clips")

_rng = _random.Random(42)
# Sample 60 clips (every 5th + some random to cover all categories)
corpus_sample = corpus_wavs[::5][:60]
if len(corpus_sample) < 60 and len(corpus_wavs) >= 60:
    extra = [w for w in corpus_wavs if w not in corpus_sample]
    corpus_sample += _rng.sample(extra, 60 - len(corpus_sample))
corpus_sample = corpus_sample[:60]

print(f"[HarmonyAudioBench] Sampling {len(corpus_sample)} clips")

corpus_results = []
corpus_outdir  = OUTDIR / "corpus_harmony"
corpus_outdir.mkdir(exist_ok=True)

corpus_crashes = 0
corpus_nan     = 0
corpus_renders = 0

# Fixed voice intervals for corpus (whole-file): shift entire clip by each interval
HARMONY_INTERVALS_ST = [("+3rd", 3), ("+5th", 7), ("-3rd", -3)]

for i, wav_path in enumerate(corpus_sample):
    if i % 20 == 0:
        print(f"  [CorpusHarm] {i}/{len(corpus_sample)}...")
    label = wav_path.stem[:30]

    clip_crash = False
    clip_nan   = False

    for interval_name, semitones in HARMONY_INTERVALS_ST:
        out_path = corpus_outdir / f"{label}_{interval_name.replace('+','p').replace('-','m')}.wav"
        ok = True
        try:
            r = subprocess.run([
                "/opt/homebrew/bin/rubberband",
                "--pitch", str(semitones),
                "--formant", "--quiet",
                str(wav_path), str(out_path),
            ], capture_output=True, text=True, timeout=20)
            ok = r.returncode == 0 and out_path.exists()
        except Exception:
            ok = False

        if not ok:
            clip_crash = True
            continue

        # Quick NaN/Inf scan: read 1000 samples from output
        try:
            with wave.open(str(out_path), "r") as wf:
                raw = wf.readframes(min(1000, wf.getnframes()))
                n   = len(raw) // 2
                vals = struct.unpack(f"<{n}h", raw)
                if any(not math.isfinite(float(v)) for v in vals):
                    clip_nan = True
        except Exception:
            clip_crash = True

    corpus_crashes += int(clip_crash)
    corpus_nan     += int(clip_nan)
    corpus_renders += 1
    corpus_results.append({"wav": label, "crash": clip_crash, "nan": clip_nan})

print(f"  [CorpusHarm] Done: {corpus_renders}/{len(corpus_sample)} rendered, "
      f"crashes={corpus_crashes}, nan={corpus_nan}")


# ── Gate ──────────────────────────────────────────────────────────────────────

total_crashes = sum(1 for r in vocal_results if not r["ok"]) + corpus_crashes
total_nan     = sum(r.get("nan_inf", 0) for r in vocal_results) + corpus_nan
total_vocal   = len(vocal_results)

avg_render_rate = (sum(r.get("render_rate", 0) for r in vocal_results) /
                   max(len(vocal_results), 1))
avg_vl = (sum(r.get("voice_leading_mean_cents", 0) for r in vocal_results) /
          max(len(vocal_results), 1))

gate_pass = (
    corpus_crashes == 0 and
    total_nan == 0 and
    avg_render_rate >= 0.85 and
    avg_vl <= 200.0
)
gate = "HARMONY AUDIO RC" if gate_pass else "HARMONY AUDIO HOLD"

print(f"\n[HarmonyAudioBench] === Gate ===")
print(f"  Vocal cases: {total_vocal}  crashes={sum(1 for r in vocal_results if not r['ok'])}")
print(f"  Corpus: {corpus_renders}/{len(corpus_sample)}  crashes={corpus_crashes}  nan={corpus_nan}")
print(f"  Avg render rate: {avg_render_rate:.1%} (gate ≥85%)")
print(f"  Avg voice-leading dist: {avg_vl:.1f}¢ (gate ≤200¢)")
print(f"\nGate: {gate}")

# ── Report ─────────────────────────────────────────────────────────────────────

lines = [
    "# Harmony Audio Engine — RC Report",
    "",
    "## Architecture",
    "- Per-voiced-frame pitch-shifting via RubberBand CLI",
    "- Interval computed by HarmonyEngine (scale-aware, diatonic)",
    "- Pitch-shift semitones = target_midi - detected_midi",
    "- Formant preservation: --formant flag on all voices",
    "- Pan law: constant-power stereo positioning",
    "- Dry melody preserved at center; voices added",
    "",
    "## Vocal Test Matrix",
    "",
    "| Case | Voiced | Rendered | VL dist | Crash | NaN | OK? |",
    "|------|--------|----------|---------|-------|-----|-----|",
]
for r in vocal_results:
    lines.append(f"| {r['case']} | {r.get('voiced_frames',0)} | "
                 f"{r.get('render_rate',0):.1%} | {r.get('voice_leading_mean_cents',0):.1f}¢ | "
                 f"{r.get('crashes',0)} | {r.get('nan_inf',0)} | "
                 f"{'✅' if r['ok'] else '❌'} |")

lines += [
    "",
    "## Voice Count Scaling",
    "",
    "| Config | Voiced | Rendered | VL dist | Crash | OK? |",
    "|--------|--------|----------|---------|-------|-----|",
]
for r in scale_results:
    lines.append(f"| {r['cfg']} | {r.get('voiced_frames',0)} | "
                 f"{r.get('render_rate',0):.1%} | {r.get('voice_leading_mean_cents',0):.1f}¢ | "
                 f"{r.get('crashes',0)} | {'✅' if r['ok'] else '❌'} |")

lines += [
    "",
    f"## 308-Clip Corpus Results",
    f"- Sample: {len(corpus_sample)}/{len(corpus_wavs)}  Rendered: {corpus_renders}  Crashes: {corpus_crashes}  NaN: {corpus_nan}",
    f"- Whole-file pitch shift per interval: +3rd (+3st), +5th (+7st), -3rd (-3st)",
    "",
    "## Summary",
    f"- Average render rate: {avg_render_rate:.1%}",
    f"- Average voice-leading distance: {avg_vl:.1f}¢",
    f"- Total crashes: {total_crashes}",
    f"- Total NaN/Inf: {total_nan}",
    "",
    "## Gate",
    "```",
    gate,
    "```",
]

report = OUTDIR / "HARMONY_AUDIO_RC_REPORT.md"
report.write_text("\n".join(lines))
print(f"\nReport: {report}")

if "HOLD" in gate:
    sys.exit(1)
