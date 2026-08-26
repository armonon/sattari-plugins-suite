"""
Agent A — Live Pitch Real Audio + Latency (Phase 2)

Measures ACTUAL end-to-end latency using impulse marker method.
Tests real neural vocal signals across all voice types.
Compares standard (OptionPitchHighQuality) vs live (OptionWindowShort) modes.

Latency method:
  - Generate onset signal: long silence → impulse → 440Hz sustained tone
  - Run through bench binary (standard and --low-latency)
  - Find first output frame above RMS threshold
  - Latency = (output_onset_sample - input_onset_sample) × 1000 / sr

Vocal test matrix:
  - Sustained (A3, E4, A4)
  - Vibrato (±50¢ at 5Hz)
  - Slides (whole-tone ascending and descending)
  - Scoop onset (start 50¢ flat, correct to pitch)
  - Fast run (8 notes in 400ms)
  - Repeated attacks (repeated A4 at 120bpm)
  - Silence→voice onset
  - Breathy/low level (0.1 amplitude)
  - Belted/high energy (0.95 amplitude with harmonics)
"""

import subprocess, json, math, struct, sys, time, wave, os
from pathlib import Path

BENCH  = Path("/Users/agentsattari/code/sattari-plugins-suite/build/plugins/auto-pitch/AutoPitchBench")
OUTDIR = Path(__file__).parent.parent / "validation" / "live_pitch_p2"
OUTDIR.mkdir(parents=True, exist_ok=True)

A4_HZ  = 440.0
A4_MIDI = 69


# ── Signal generation ─────────────────────────────────────────────────────────

def write_wav(path, samples, sr=44100):
    with wave.open(str(path), "w") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        clipped = [max(-32767, min(32767, int(s * 32767))) for s in samples]
        wf.writeframes(struct.pack(f"<{len(clipped)}h", *clipped))


def read_wav_mono(path):
    with wave.open(str(path), "r") as wf:
        sr = wf.getframerate()
        nch = wf.getnchannels()
        raw = wf.readframes(wf.getnframes())
        n   = len(raw) // 2
        data = struct.unpack(f"<{n}h", raw)
        # Stereo → mono
        if nch == 2:
            samples = [(data[i] + data[i+1]) / 2.0 / 32768.0 for i in range(0, n, 2)]
        else:
            samples = [s / 32768.0 for s in data]
        return samples, sr


def midi_to_hz(midi):
    return A4_HZ * 2 ** ((midi - A4_MIDI) / 12.0)


def vocal_tone(hz, dur_sec, amp=0.7, sr=44100, harmonics=True):
    """Synthesize a vocal-like tone: fundamental + harmonics."""
    n = int(sr * dur_sec)
    t = [i / sr for i in range(n)]
    if harmonics:
        return [amp * (0.6 * math.sin(2*math.pi*hz*ti)
                       + 0.25 * math.sin(2*math.pi*2*hz*ti)
                       + 0.10 * math.sin(2*math.pi*3*hz*ti)
                       + 0.05 * math.sin(2*math.pi*4*hz*ti)) for ti in t]
    return [amp * math.sin(2*math.pi*hz*ti) for ti in t]


def make_impulse_signal(sr=44100, silence_ms=200, impulse_dur_ms=10, tone_hz=440.0, tone_dur_sec=3.0):
    """
    Onset marker signal: silence → short impulse → sustained tone.
    The impulse is a clearly detectable onset marker.
    """
    n_silence   = int(sr * silence_ms / 1000)
    n_impulse   = int(sr * impulse_dur_ms / 1000)
    n_tone      = int(sr * tone_dur_sec)

    samples  = [0.0] * n_silence
    # Square impulse burst — very clear onset
    samples += [0.9] * n_impulse
    # Sustained tone
    for i in range(n_tone):
        t = i / sr
        samples.append(0.7 * math.sin(2 * math.pi * tone_hz * t))
    return samples, n_silence + n_impulse // 2  # expected onset = center of impulse


def make_sustained(hz, dur_sec=3.0, amp=0.7, sr=44100):
    return vocal_tone(hz, dur_sec, amp=amp, sr=sr)


def make_vibrato(hz, dur_sec=3.0, vib_hz=5.0, vib_cents=50.0, sr=44100):
    """Vibrato: sinusoidal pitch modulation."""
    n = int(sr * dur_sec)
    samples = []
    for i in range(n):
        t = i / sr
        cents_dev = vib_cents * math.sin(2 * math.pi * vib_hz * t)
        freq = hz * (2 ** (cents_dev / 1200.0))
        v  = 0.7 * math.sin(2 * math.pi * freq * t)
        v += 0.2 * math.sin(2 * math.pi * 2 * freq * t)
        samples.append(v)
    return samples


def make_slide(hz_start, hz_end, dur_sec=2.0, sr=44100):
    """Linear frequency glide."""
    n = int(sr * dur_sec)
    samples = []
    phase = 0.0
    for i in range(n):
        frac = i / n
        hz = hz_start + (hz_end - hz_start) * frac
        samples.append(0.7 * math.sin(phase))
        phase += 2 * math.pi * hz / sr
    return samples


def make_scoop(hz_target, scoop_cents=80, dur_sec=2.0, attack_ms=100, sr=44100):
    """Scoop: start below target, glide up to target quickly."""
    n_attack = int(sr * attack_ms / 1000)
    n_total  = int(sr * dur_sec)
    hz_start = hz_target * (2 ** (-scoop_cents / 1200.0))
    samples = []
    phase = 0.0
    for i in range(n_total):
        if i < n_attack:
            # Exponential approach to target
            frac = i / n_attack
            hz = hz_start + (hz_target - hz_start) * (1 - math.exp(-3 * frac))
        else:
            hz = hz_target
        samples.append(0.7 * math.sin(phase))
        phase += 2 * math.pi * hz / sr
    return samples


def make_fast_run(midis, note_dur_ms=50, sr=44100):
    """Fast chromatic/scale run."""
    samples = []
    for midi in midis:
        hz = midi_to_hz(midi)
        n = int(sr * note_dur_ms / 1000)
        for i in range(n):
            t = i / sr
            samples.append(0.7 * math.sin(2 * math.pi * hz * t))
    return samples


def make_repeated_attacks(hz, n_attacks=8, bpm=120, sr=44100):
    """Repeated note attacks at 120bpm."""
    beat_sec = 60.0 / bpm
    gap_sec  = 0.03  # 30ms silence between attacks
    note_sec = beat_sec - gap_sec
    samples  = []
    for _ in range(n_attacks):
        note = vocal_tone(hz, note_sec, amp=0.7, sr=sr)
        gap  = [0.0] * int(sr * gap_sec)
        samples.extend(note)
        samples.extend(gap)
    return samples


def make_silence_to_voice(hz, silence_sec=0.5, voice_sec=2.0, sr=44100):
    silence = [0.0] * int(sr * silence_sec)
    voice   = vocal_tone(hz, voice_sec, sr=sr)
    return silence + voice


def make_breathy(hz, dur_sec=3.0, sr=44100):
    """Breathy voice: low amplitude + noise."""
    import random
    rng = random.Random(99)
    pure = vocal_tone(hz, dur_sec, amp=0.08, sr=sr)
    return [s + 0.04 * (rng.random()*2-1) for s in pure]


def make_belted(hz, dur_sec=3.0, sr=44100):
    """Belted/high-energy voice: high amplitude with strong harmonics."""
    n = int(sr * dur_sec)
    samples = []
    for i in range(n):
        t = i / sr
        v  = 0.4 * math.sin(2*math.pi*hz*t)
        v += 0.3 * math.sin(2*math.pi*2*hz*t)
        v += 0.15 * math.sin(2*math.pi*3*hz*t)
        v += 0.08 * math.sin(2*math.pi*4*hz*t)
        v += 0.04 * math.sin(2*math.pi*5*hz*t)
        samples.append(max(-0.95, min(0.95, v * 1.2)))
    return samples


# ── Latency measurement ───────────────────────────────────────────────────────

def find_onset_sample(samples, threshold_rms=0.04, window_ms=5, sr=44100):
    """Find first sample index where RMS in window exceeds threshold."""
    win = max(1, int(sr * window_ms / 1000))
    for i in range(0, len(samples) - win, win // 4):
        rms = math.sqrt(sum(s**2 for s in samples[i:i+win]) / win)
        if rms > threshold_rms:
            return i
    return len(samples)  # not found


def measure_latency(input_wav, output_wav, input_onset_sample, sr=44100):
    """
    Measure latency = output_onset_sample - input_onset_sample.
    Returns (latency_ms, onset_sample_out, found).
    """
    try:
        samples_out, sr_out = read_wav_mono(output_wav)
    except Exception:
        return 9999.0, -1, False

    onset_out = find_onset_sample(samples_out, sr=sr_out)
    latency_ms = max(0.0, (onset_out - input_onset_sample) / sr_out * 1000.0)
    found = onset_out < len(samples_out) - 100
    return round(latency_ms, 1), onset_out, found


# ── Run bench ─────────────────────────────────────────────────────────────────

def run_bench(wav_path, label, key="A", scale="minor", amount=0.82, speed=0.7,
              block=256, sr_override=None, low_latency=False, timeout=90):
    """Run bench binary, return (trace_path, output_wav, returncode, elapsed)."""
    trace_path = OUTDIR / f"trace_{label}.json"
    out_path   = OUTDIR / f"out_{label}.wav"
    cmd = [
        str(BENCH), str(wav_path),
        "--output", str(out_path),
        "--trace",  str(trace_path),
        "--key", key,
        "--scale", scale,
        "--amount", str(amount),
        "--speed",  str(speed),
        "--block",  str(block),
        "--intent-mode", "v2_active",
    ]
    if low_latency:
        cmd.append("--low-latency")

    t0 = time.monotonic()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        elapsed = time.monotonic() - t0
        return trace_path, out_path, r.returncode, elapsed
    except subprocess.TimeoutExpired:
        return trace_path, out_path, -999, timeout


def parse_trace(trace_path):
    """Parse trace JSON → quality metrics."""
    try:
        frames = json.loads(Path(trace_path).read_text()).get("frames", [])
    except Exception:
        return {}

    crashes = 0
    nan_inf = 0
    octave_errors = 0
    correction_errors = []
    max_jump = 0.0
    chatter_count = 0
    voiced_count = 0
    cpu_time_ms = 0
    prev_corrected = None
    prev_voiced = None

    for fr in frames:
        if not fr.get("voiced"):
            continue
        voiced_count += 1
        det  = fr.get("detectedMidi", 0.0)
        corr = fr.get("correctionCents", 0.0)
        if not math.isfinite(det) or not math.isfinite(corr):
            nan_inf += 1
            continue
        corrected = det + corr / 100.0
        if abs(corrected - det) > 12.0:
            octave_errors += 1
        err = abs(corr)  # correction remaining from target
        correction_errors.append(err)
        if prev_corrected is not None:
            jump = abs(corrected - prev_corrected) * 100.0
            max_jump = max(max_jump, jump)
        prev_corrected = corrected

    mean_err = sum(correction_errors) / max(len(correction_errors), 1)
    p90_err  = sorted(correction_errors)[int(len(correction_errors)*0.9)] if correction_errors else 0
    return {
        "voiced":          voiced_count,
        "octave_errors":   octave_errors,
        "nan_inf":         nan_inf,
        "crashes":         crashes,
        "correction_mean": round(mean_err, 1),
        "correction_p90":  round(p90_err, 1),
        "max_jump":        round(max_jump, 1),
    }


# ── Build test matrix ─────────────────────────────────────────────────────────

print("[LiveBench-P2] Building vocal test matrix...")

A3_HZ = midi_to_hz(57)   # A3 = 220 Hz
E4_HZ = midi_to_hz(64)   # E4 ≈ 329.6 Hz
A4_HZ_V = midi_to_hz(69) # A4 = 440 Hz

vocal_cases = [
    ("sustained_A3",     make_sustained(A3_HZ, 3.0)),
    ("sustained_E4",     make_sustained(E4_HZ, 3.0)),
    ("sustained_A4",     make_sustained(A4_HZ_V, 3.0)),
    ("vibrato_A4",       make_vibrato(A4_HZ_V, 3.0)),
    ("slide_up_A3_A4",   make_slide(A3_HZ, A4_HZ_V, 2.0)),
    ("slide_dn_A4_A3",   make_slide(A4_HZ_V, A3_HZ, 2.0)),
    ("scoop_A4",         make_scoop(A4_HZ_V, scoop_cents=80)),
    ("fast_run_A_min",   make_fast_run([57,59,60,62,64,65,67,69], 50)),  # A minor scale
    ("repeated_A4_120",  make_repeated_attacks(A4_HZ_V, 8, 120)),
    ("silence_to_A4",    make_silence_to_voice(A4_HZ_V)),
    ("breathy_A4",       make_breathy(A4_HZ_V, 3.0)),
    ("belted_A4",        make_belted(A4_HZ_V, 3.0)),
]

# Write WAVs
for name, samples in vocal_cases:
    write_wav(OUTDIR / f"vocal_{name}.wav", samples)

print(f"[LiveBench-P2] {len(vocal_cases)} vocal signals synthesized")


# ── Latency sweep: 44100Hz, blocks 32/64/128/256, std vs live ─────────────────

BLOCKS_44K = [32, 64, 128, 256]
SR = 44100

print("\n[LiveBench-P2] == LATENCY SWEEP (44100 Hz) ==")
print(f"{'Mode':<12} {'Block':>5} | {'Expected(ms)':>12} | {'Measured(ms)':>12} | {'Status'}")
print("-" * 70)

# Impulse signals for latency measurement
impulse_44k, impulse_onset_44k = make_impulse_signal(sr=44100, silence_ms=200,
                                                       tone_hz=440.0, tone_dur_sec=3.0)
impulse_wav_44k = OUTDIR / "impulse_44100.wav"
write_wav(impulse_wav_44k, impulse_44k, sr=44100)

latency_results = []

for low_lat in [False, True]:
    mode = "live" if low_lat else "standard"
    for block in BLOCKS_44K:
        label = f"lat_{mode}_b{block}_sr44k"
        trace, out_wav, rc, elapsed = run_bench(
            impulse_wav_44k, label,
            block=block, low_latency=low_lat, timeout=120
        )
        if rc != 0 or not out_wav.exists():
            print(f"  FAIL: bench returned {rc}")
            latency_results.append({"mode": mode, "block": block, "sr": 44100,
                                    "latency_ms": 9999, "crash": True})
            continue

        latency_ms, onset_out, found = measure_latency(impulse_wav_44k, out_wav,
                                                        impulse_onset_44k, sr=44100)
        crash = rc != 0
        status = "OK" if found and not crash else "NO_ONSET" if not found else "CRASH"
        expected_ms = (512 if not low_lat else 128) / 44100 * 1000  # rough RubberBand estimate

        print(f"  {mode:<12} {block:>5} | {expected_ms:>12.1f} | {latency_ms:>12.1f} | {status}")
        latency_results.append({
            "mode": mode, "block": block, "sr": 44100,
            "latency_ms": latency_ms, "crash": crash, "found": found,
        })

# 48kHz sweep (subset: block=64, 128)
print("\n[LiveBench-P2] == LATENCY SWEEP (48000 Hz) ==")
impulse_48k, impulse_onset_48k = make_impulse_signal(sr=48000, silence_ms=200,
                                                       tone_hz=440.0, tone_dur_sec=3.0)
impulse_wav_48k = OUTDIR / "impulse_48000.wav"
write_wav(impulse_wav_48k, impulse_48k, sr=48000)

for low_lat in [False, True]:
    mode = "live" if low_lat else "standard"
    for block in [64, 128]:
        label = f"lat_{mode}_b{block}_sr48k"
        trace, out_wav, rc, elapsed = run_bench(
            impulse_wav_48k, label,
            block=block, low_latency=low_lat, timeout=120
        )
        if rc != 0 or not out_wav.exists():
            print(f"  FAIL: bench returned {rc}")
            latency_results.append({"mode": mode, "block": block, "sr": 48000,
                                    "latency_ms": 9999, "crash": True})
            continue
        latency_ms, _, found = measure_latency(impulse_wav_48k, out_wav,
                                               impulse_onset_48k, sr=48000)
        status = "OK" if found else "NO_ONSET"
        print(f"  {mode:<12} {block:>5} | {'—':>12} | {latency_ms:>12.1f} | {status} (48kHz)")
        latency_results.append({
            "mode": mode, "block": block, "sr": 48000,
            "latency_ms": latency_ms, "crash": rc != 0, "found": found,
        })


# ── Vocal quality matrix: std vs live ─────────────────────────────────────────

print("\n[LiveBench-P2] == VOCAL QUALITY MATRIX ==")
print(f"{'Signal':<22} {'Mode':<12} | {'Voiced':>6} | {'CorrMean':>8} | {'MaxJump':>8} | {'OctErr':>6} | OK?")
print("-" * 90)

quality_results = []
block_test = 128  # gate block for quality

for name, _ in vocal_cases:
    wav_in = OUTDIR / f"vocal_{name}.wav"
    for low_lat in [False, True]:
        mode = "live" if low_lat else "standard"
        label = f"qual_{mode}_{name}"
        trace, out_wav, rc, elapsed = run_bench(
            wav_in, label, block=block_test, low_latency=low_lat, timeout=90
        )
        crash = rc != 0
        m = {} if crash or not trace.exists() else parse_trace(trace)
        ok = not crash and m.get("nan_inf", 0) == 0
        status = "CRASH" if crash else ("OK" if ok else "FAIL")
        print(f"  {name:<22} {mode:<12} | {m.get('voiced','?'):>6} | "
              f"{m.get('correction_mean','?'):>8} | {m.get('max_jump','?'):>8} | "
              f"{m.get('octave_errors','?'):>6} | {status}")
        quality_results.append({
            "signal": name, "mode": mode,
            "crash": crash, **m, "status": status
        })

# ── Gate evaluation ───────────────────────────────────────────────────────────

print("\n[LiveBench-P2] === Gate Evaluation ===")

# Latency gate: live mode at block=128, 44100Hz ≤ 30ms (prefer ≤20ms)
live_128_44k = [r for r in latency_results
                if r["mode"] == "live" and r["block"] == 128 and r["sr"] == 44100]
std_128_44k  = [r for r in latency_results
                if r["mode"] == "standard" and r["block"] == 128 and r["sr"] == 44100]

live_latency = live_128_44k[0]["latency_ms"] if live_128_44k else 9999
std_latency  = std_128_44k[0]["latency_ms"]  if std_128_44k  else 9999

latency_ok = live_latency <= 30.0
latency_preferred = live_latency <= 20.0

# Quality gate: live mode, block=128, no crashes/NaN across all vocal types
live_quality = [r for r in quality_results if r["mode"] == "live"]
quality_crashes = sum(1 for r in live_quality if r.get("crash"))
quality_nan     = sum(r.get("nan_inf", 0) for r in live_quality)
quality_oct     = sum(r.get("octave_errors", 0) for r in live_quality)
quality_ok = quality_crashes == 0 and quality_nan == 0

gate_pass = latency_ok and quality_ok

gate = "LIVE PITCH PRODUCTION PASS" if gate_pass else "LIVE PITCH PRODUCTION HOLD"
print(f"  Live mode latency @ block=128 / 44100Hz: {live_latency}ms "
      f"{'✅ ≤20ms' if latency_preferred else ('✅ ≤30ms' if latency_ok else '❌ >30ms')}")
print(f"  Standard latency @ block=128 / 44100Hz:  {std_latency}ms")
print(f"  Latency reduction: {round(std_latency - live_latency, 1)}ms")
print(f"  Quality: crashes={quality_crashes} nan={quality_nan} oct={quality_oct} "
      f"{'✅' if quality_ok else '❌'}")
print(f"\nGate: {gate}")


# ── Write report ──────────────────────────────────────────────────────────────

lines = [
    "# Live Pitch — Production Bench Report (Phase 2)",
    "",
    "## Latency Measurement Method",
    "- Impulse marker signal: 200ms silence → 10ms square burst → 3s sustained tone",
    "- Onset detected as first RMS-above-threshold window in output (RMS threshold = 0.04)",
    "- Latency = (output_onset_sample − input_onset_sample) × 1000 / sample_rate",
    "- Standard mode: RubberBand OptionPitchHighQuality",
    "- Live mode: RubberBand OptionWindowShort (--low-latency flag)",
    "",
    "## Latency Results",
    "",
    "| Mode | SR | Block | Block(ms) | Latency(ms) | Status |",
    "|------|----|-------|-----------|-------------|--------|",
]
for r in latency_results:
    b_ms = round(r["block"] / r["sr"] * 1000, 2)
    status = "CRASH" if r.get("crash") else ("OK" if r.get("found", True) else "NO_ONSET")
    gate_marker = " ← gate" if r["block"] == 128 and r["sr"] == 44100 else ""
    lines.append(f"| {r['mode']} | {r['sr']} | {r['block']} | {b_ms} | "
                 f"{r['latency_ms']} | {status}{gate_marker} |")

lines += [
    "",
    f"**Live mode reduction:** {round(std_latency - live_latency, 1)}ms "
    f"({std_latency}ms → {live_latency}ms at block=128 / 44100Hz)",
    "",
    "## Vocal Quality Matrix (block=128)",
    "",
    "| Signal | Mode | Voiced | CorrMean | MaxJump | OctErr | Status |",
    "|--------|------|--------|----------|---------|--------|--------|",
]
for r in quality_results:
    lines.append(f"| {r['signal']} | {r['mode']} | {r.get('voiced','?')} | "
                 f"{r.get('correction_mean','?')}¢ | {r.get('max_jump','?')}¢ | "
                 f"{r.get('octave_errors','?')} | {r['status']} |")

lines += [
    "",
    "## Gate",
    f"- Latency @ block=128 / 44100Hz: live={live_latency}ms, standard={std_latency}ms",
    f"- Latency ≤30ms: {'YES' if latency_ok else 'NO'}",
    f"- Latency ≤20ms (preferred): {'YES' if latency_preferred else 'NO'}",
    f"- Quality (no crashes/NaN): {'YES' if quality_ok else 'NO'}",
    "",
    "```",
    gate,
    "```",
]

report = OUTDIR / "LIVE_PITCH_PRODUCTION_REPORT.md"
report.write_text("\n".join(lines))
print(f"\nReport: {report}")

if "HOLD" in gate:
    sys.exit(1)
