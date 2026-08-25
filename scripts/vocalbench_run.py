#!/usr/bin/env python3
"""VocalBench v2 — Auto Pitch Engine Research Laboratory.

Suites:
  stable    39 cases A2/A3/A4 at 0/±5/±10/±20/±30/±40/±49¢ (default)
  duration  Phase 3: time-to-target across 250ms–8s signal durations
  boundary  Phase 5: semitone decision-boundary sweep ±40–±60¢
  vibrato   Phase 6: deterministic pitch modulation (TODO)
  full      stable + duration + boundary
  nightly   stable + duration (shorter run)
  research  stable + boundary (failure discovery mode)

Usage:
    python3 scripts/vocalbench_run.py \\
        --bench build/plugins/auto-pitch/AutoPitchBench \\
        [--suite stable] [--output vocalbench-results] \\
        [--key A] [--scale minor] [--amount 1.0] [--speed 0.9] \\
        [--block 512] [--dry-run]
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
import subprocess
import sys
import wave
from pathlib import Path

# ---------------------------------------------------------------------------
# Repository layout
# ---------------------------------------------------------------------------

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT  = SCRIPT_DIR.parent

# ---------------------------------------------------------------------------
# Test corpus definitions
# ---------------------------------------------------------------------------

TEST_NOTES = [
    ("A2", 45, 110.0),
    ("A3", 57, 220.0),
    ("A4", 69, 440.0),
]

CENT_OFFSETS_STABLE = [0, +5, -5, +10, -10, +20, -20, +30, -30, +40, -40, +49, -49]

CENT_OFFSETS_BOUNDARY = [
    +40, +45, +47, +48, +49, +49.5, +50, +50.5, +51, +52, +55, +60,
    -40, -45, -47, -48, -49, -49.5, -50, -50.5, -51, -52, -55, -60,
]

DURATION_OFFSETS = [+10, -10, +20, -20, +30, -30, +40, -40, +49, -49]

DURATION_SECS = [0.25, 0.5, 1.0, 2.0, 4.0, 8.0]

SIGNAL_SAMPLE_RATE = 48000
SIGNAL_AMPLITUDE   = 0.20

TARGET_CORRECT_THRESH_CENTS = 50.0
OCTAVE_ERROR_LO_CENTS       = 700.0
OCTAVE_ERROR_HI_CENTS       = 1500.0

ACF_WINDOW_SEC = 0.05
ACF_CONF_FLOOR = 0.05

TIME_CHECKPOINTS_SEC = [0.05, 0.10, 0.20, 0.25, 0.50, 0.75, 1.0, 2.0, 4.0]
TIME_TO_THRESHOLDS_CENTS = [20.0, 10.0, 5.0]

# ---------------------------------------------------------------------------
# Helpers — math
# ---------------------------------------------------------------------------

def hz_to_midi(hz: float) -> float:
    return 69.0 + 12.0 * math.log2(hz / 440.0) if hz > 0.0 else -1.0


def midi_to_hz(midi: float) -> float:
    return 440.0 * (2.0 ** ((midi - 69.0) / 12.0))


def cents_between(hz_a: float, hz_b: float) -> float:
    if hz_a <= 0.0 or hz_b <= 0.0:
        return 0.0
    return 1200.0 * math.log2(hz_a / hz_b)


def scale_to_cents(s: float) -> float:
    return 1200.0 * math.log2(s) if s > 0.0 else 0.0


def correction_efficiency(input_error_cents: float,
                           output_error_cents: float) -> float | None:
    if input_error_cents == 0.0:
        return None
    eff = 1.0 - abs(output_error_cents) / abs(input_error_cents)
    return eff


# ---------------------------------------------------------------------------
# Helpers — statistics
# ---------------------------------------------------------------------------

def percentile(values: list[float], p: float) -> float:
    if not values:
        return 0.0
    s = sorted(values)
    idx = (len(s) - 1) * p / 100.0
    lo, hi = int(idx), min(int(idx) + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (idx - lo)


def compute_stats(values: list[float]) -> dict:
    if not values:
        return {}
    n    = len(values)
    mean = sum(values) / n
    sq   = sum((v - mean) ** 2 for v in values)
    sd   = math.sqrt(sq / n) if n > 1 else 0.0
    return {
        "n":      n,
        "mean":   round(mean, 3),
        "median": round(percentile(values, 50), 3),
        "p90":    round(percentile(values, 90), 3),
        "p95":    round(percentile(values, 95), 3),
        "max":    round(max(values), 3),
        "stdev":  round(sd, 3),
    }


# ---------------------------------------------------------------------------
# WAV I/O
# ---------------------------------------------------------------------------

def write_wav_16bit(path: Path, samples: list[float],
                    sr: int = 48000) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    max_val = 32767
    pcm = struct.pack(f"<{len(samples)}h",
                      *(int(max(-32768, min(max_val, round(s * max_val))))
                        for s in samples))
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm)


def read_wav_mono(path: Path) -> tuple[list[float], int]:
    with wave.open(str(path), "rb") as w:
        channels  = w.getnchannels()
        sampwidth = w.getsampwidth()
        sr        = w.getframerate()
        n_frames  = w.getnframes()
        raw       = w.readframes(n_frames)

    samples: list[float] = []
    if sampwidth == 2:
        fmt  = f"<{n_frames * channels}h"
        vals = struct.unpack(fmt, raw)
        scale = 1.0 / 32768.0
        for i in range(n_frames):
            v = sum(vals[i * channels + c] for c in range(channels)) / channels
            samples.append(v * scale)
    elif sampwidth == 3:
        scale = 1.0 / 8388608.0
        mv = memoryview(raw)
        frame_bytes = 3 * channels
        for i in range(n_frames):
            v = 0.0
            for c in range(channels):
                offset = i * frame_bytes + c * 3
                word = int.from_bytes(mv[offset:offset + 3], "little", signed=True)
                v += word
            samples.append(v / channels * scale)
    elif sampwidth == 4:
        fmt  = f"<{n_frames * channels}i"
        vals = struct.unpack(fmt, raw)
        scale = 1.0 / 2147483648.0
        for i in range(n_frames):
            v = sum(vals[i * channels + c] for c in range(channels)) / channels
            samples.append(v * scale)
    else:
        raise ValueError(f"Unsupported WAV sample width: {sampwidth}")

    return samples, sr


# ---------------------------------------------------------------------------
# Signal generation
# ---------------------------------------------------------------------------

def generate_stable_tone(hz: float, sr: int, duration: float,
                          amplitude: float = SIGNAL_AMPLITUDE) -> list[float]:
    """4-harmonic pitched tone with 10 ms linear fade-in."""
    n        = int(round(duration * sr))
    fade_n   = int(round(0.010 * sr))
    samples  = []
    phase    = 0.0
    two_pi_f = 2.0 * math.pi * hz / sr
    for i in range(n):
        s  = 1.000 * math.sin(phase)
        s += 0.450 * math.sin(2.0 * phase)
        s += 0.250 * math.sin(3.0 * phase)
        s += 0.120 * math.sin(4.0 * phase)
        env = i / fade_n if i < fade_n else 1.0
        samples.append(amplitude * s * env)
        phase += two_pi_f
    return samples


def generate_vibrato_tone(center_hz: float, sr: int, duration: float,
                           rate_hz: float, depth_cents: float,
                           amplitude: float = SIGNAL_AMPLITUDE) -> list[float]:
    """Sinusoidal vibrato at specified rate and depth."""
    n        = int(round(duration * sr))
    fade_n   = int(round(0.010 * sr))
    samples  = []
    phase    = 0.0
    vib_phase = 0.0
    two_pi_v = 2.0 * math.pi * rate_hz / sr
    for i in range(n):
        # Instantaneous frequency with vibrato
        vib_ratio = 2.0 ** (depth_cents * math.sin(vib_phase) / 1200.0)
        inst_hz   = center_hz * vib_ratio
        phase    += 2.0 * math.pi * inst_hz / sr
        s  = 1.000 * math.sin(phase)
        s += 0.450 * math.sin(2.0 * phase)
        s += 0.250 * math.sin(3.0 * phase)
        s += 0.120 * math.sin(4.0 * phase)
        env = i / fade_n if i < fade_n else 1.0
        samples.append(amplitude * s * env)
        vib_phase += two_pi_v
    return samples


# ---------------------------------------------------------------------------
# Independent pitch estimator — normalized ACF (not YIN)
# ---------------------------------------------------------------------------

def estimate_pitch_acf(samples: list[float], sr: int,
                        min_hz: float = 60.0,
                        max_hz: float = 800.0,
                        window_sec: float = ACF_WINDOW_SEC,
                        start_offset: float = 0.0) -> tuple[float, float]:
    """
    Normalized autocorrelation estimator — completely independent of production YIN.

    If start_offset > 0, analyze a window starting at that time (seconds).
    Returns (freq_hz, confidence).
    """
    n        = len(samples)
    win_size = min(n, int(sr * window_sec))
    if start_offset > 0.0:
        start = min(n - win_size, int(start_offset * sr))
    else:
        start = max(0, (n - win_size) // 2)
    window = samples[start:start + win_size]
    w      = len(window)

    energy = sum(s * s for s in window)
    if energy < 1.0e-8:
        return 0.0, 0.0

    min_lag = max(1, int(sr / max_hz))
    max_lag = min(w // 2, int(sr / min_hz))
    if max_lag <= min_lag:
        return 0.0, 0.0

    acf: list[float] = []
    for tau in range(min_lag, max_lag + 1):
        r = sum(window[i] * window[i + tau] for i in range(w - tau)) / energy
        acf.append(r)

    if not acf:
        return 0.0, 0.0

    best_idx   = max(range(len(acf)), key=lambda i: acf[i])
    best_lag   = min_lag + best_idx
    confidence = acf[best_idx]

    if confidence < ACF_CONF_FLOOR:
        return 0.0, 0.0

    refined_lag = float(best_lag)
    if 0 < best_idx < len(acf) - 1:
        y0, y1, y2 = acf[best_idx - 1], acf[best_idx], acf[best_idx + 1]
        denom = y0 - 2.0 * y1 + y2
        if abs(denom) > 1.0e-10:
            refined_lag = best_lag - 0.5 * (y0 - y2) / denom

    freq_hz = sr / refined_lag
    return freq_hz, confidence


# ---------------------------------------------------------------------------
# Convergence analysis from trace frames (Phase 3 / Phase 4)
# ---------------------------------------------------------------------------

def analyze_convergence(frames: list[dict],
                         needed_cents: float,
                         duration_sec: float) -> dict:
    """
    Analyze smoothedPitchScale convergence from trace frames.

    Uses the production trace for convergence timing (not the independent ACF).
    The ACF is used for final output pitch scoring; convergence timing comes
    from the internal smoothedPitchScale because we need per-block resolution.
    """
    voiced = [f for f in frames if f.get("voiced")]
    if not voiced:
        return {"allUnvoiced": True}

    result: dict = {}

    # Steady-state: last 25% of voiced blocks
    n_ss = max(1, len(voiced) * 3 // 4)
    ss_frames = voiced[n_ss:]
    if ss_frames:
        ss_applied = [scale_to_cents(f.get("smoothedPitchScale", 1.0))
                      for f in ss_frames]
        ss_mean    = sum(ss_applied) / len(ss_applied)
        result["steadyStateAppliedCents"]  = round(ss_mean, 2)
        result["steadyStateResidualCents"] = round(needed_cents - ss_mean, 2)
    else:
        result["steadyStateAppliedCents"]  = None
        result["steadyStateResidualCents"] = None

    # Acquisition: first 10 voiced blocks (~100 ms at 512/48kHz)
    acq_frames = voiced[:10]
    acq_applied = [scale_to_cents(f.get("smoothedPitchScale", 1.0))
                   for f in acq_frames]
    if acq_applied:
        acq_mean = sum(acq_applied) / len(acq_applied)
        result["acquisitionAppliedCents"]  = round(acq_mean, 2)
        result["acquisitionResidualCents"] = round(needed_cents - acq_mean, 2)

    # Time-to threshold: when does |needed - applied| first drop below Xc?
    for thr in TIME_TO_THRESHOLDS_CENTS:
        t_reached: float | None = None
        for f in voiced:
            applied = scale_to_cents(f.get("smoothedPitchScale", 1.0))
            if abs(needed_cents - applied) < thr:
                t_reached = f.get("timeSec")
                break
        key = f"timeTo{int(thr)}c"
        result[key] = (round(t_reached, 4) if t_reached is not None else None)

    # Trace per-checkpoint: correction requested vs applied at each time point
    checkpoints: list[dict] = []
    for cp_t in TIME_CHECKPOINTS_SEC:
        if cp_t > duration_sec + 0.1:
            continue
        # Find the frame closest to but not after cp_t
        best = None
        for f in voiced:
            if f.get("timeSec", 0.0) <= cp_t + 0.006:
                best = f
        if best is None:
            continue
        applied = scale_to_cents(best.get("smoothedPitchScale", 1.0))
        residual = needed_cents - applied
        checkpoints.append({
            "timeSec":       cp_t,
            "appliedCents":  round(applied, 2),
            "residualCents": round(residual, 2),
            "confidence":    best.get("confidence", 0.0),
        })

    result["checkpoints"] = checkpoints
    return result


def classify_root_cause(needed_cents: float,
                         frames: list[dict],
                         output_error_cents: float | None) -> str:
    """
    Classify the primary source of correction error using production trace data.
    """
    if not frames:
        return "UNKNOWN"

    voiced = [f for f in frames if f.get("voiced")]
    if not voiced:
        return "UNKNOWN"

    last = voiced[-1]

    corr_requested = last.get("correctionCents", 0.0)
    sps_final_cents = scale_to_cents(last.get("smoothedPitchScale", 1.0))
    ss_frames = voiced[len(voiced) * 3 // 4:]
    ss_corr = ([f.get("correctionCents", 0.0) for f in ss_frames]
               if ss_frames else [corr_requested])
    avg_corr_requested = sum(ss_corr) / len(ss_corr) if ss_corr else corr_requested

    abs_detection_err = abs(avg_corr_requested - needed_cents)
    abs_smoothing_err = abs(needed_cents - sps_final_cents)
    abs_output_err    = abs(output_error_cents) if output_error_cents is not None else 0.0

    if abs_detection_err > 15.0:
        return "DETECTION"
    if abs_detection_err > 5.0:
        return "TARGET_SELECTION"
    if abs_smoothing_err > 10.0 and abs_output_err > 8.0:
        return "SMOOTHING"
    if abs_smoothing_err < 5.0 and abs_output_err > 10.0:
        return "PITCH_SHIFT_STAGE"
    if abs_output_err < 8.0:
        return "MEASUREMENT_WINDOW"
    return "UNKNOWN"


# ---------------------------------------------------------------------------
# Corpus generation
# ---------------------------------------------------------------------------

def build_test_id(note: str, cents: float, suffix: str = "") -> str:
    sign = "+" if cents >= 0 else ""
    s = f"{note}_{sign}{cents:06.1f}cents"
    if suffix:
        s += f"_{suffix}"
    return s


def generate_stable_corpus(corpus_dir: Path) -> list[dict]:
    corpus_dir.mkdir(parents=True, exist_ok=True)
    manifests = []
    for note_label, target_midi, nominal_hz in TEST_NOTES:
        for cent_offset in CENT_OFFSETS_STABLE:
            actual_hz = nominal_hz * (2.0 ** (cent_offset / 1200.0))
            tid = build_test_id(note_label, cent_offset)
            wav_path = corpus_dir / f"{tid}_input.wav"
            mfst_path = corpus_dir / f"{tid}.manifest.json"
            write_wav_16bit(wav_path,
                            generate_stable_tone(actual_hz, SIGNAL_SAMPLE_RATE, 3.0),
                            SIGNAL_SAMPLE_RATE)
            m = {
                "testId":       tid,
                "suite":        "stable",
                "noteLabel":    note_label,
                "nominalHz":    nominal_hz,
                "centOffset":   cent_offset,
                "actualHz":     round(actual_hz, 6),
                "targetMidi":   target_midi,
                "targetHz":     round(midi_to_hz(target_midi), 6),
                "durationSec":  3.0,
                "sampleRate":   SIGNAL_SAMPLE_RATE,
            }
            mfst_path.write_text(json.dumps(m, indent=2) + "\n")
            manifests.append(m)
            print(f"  corpus: {tid}")
    return manifests


def generate_duration_corpus(corpus_dir: Path) -> list[dict]:
    corpus_dir.mkdir(parents=True, exist_ok=True)
    manifests = []
    for note_label, target_midi, nominal_hz in TEST_NOTES:
        for cent_offset in DURATION_OFFSETS:
            actual_hz = nominal_hz * (2.0 ** (cent_offset / 1200.0))
            for dur in DURATION_SECS:
                dur_str = f"{dur:.2f}s".replace(".", "p")
                tid = build_test_id(note_label, cent_offset, dur_str)
                wav_path = corpus_dir / f"{tid}_input.wav"
                mfst_path = corpus_dir / f"{tid}.manifest.json"
                write_wav_16bit(wav_path,
                                generate_stable_tone(actual_hz, SIGNAL_SAMPLE_RATE, dur),
                                SIGNAL_SAMPLE_RATE)
                m = {
                    "testId":      tid,
                    "suite":       "duration",
                    "noteLabel":   note_label,
                    "nominalHz":   nominal_hz,
                    "centOffset":  cent_offset,
                    "actualHz":    round(actual_hz, 6),
                    "targetMidi":  target_midi,
                    "targetHz":    round(midi_to_hz(target_midi), 6),
                    "durationSec": dur,
                    "sampleRate":  SIGNAL_SAMPLE_RATE,
                }
                mfst_path.write_text(json.dumps(m, indent=2) + "\n")
                manifests.append(m)
                print(f"  duration corpus: {tid}")
    return manifests


def generate_boundary_corpus(corpus_dir: Path) -> list[dict]:
    corpus_dir.mkdir(parents=True, exist_ok=True)
    manifests = []
    for note_label, target_midi, nominal_hz in TEST_NOTES:
        for cent_offset in CENT_OFFSETS_BOUNDARY:
            actual_hz = nominal_hz * (2.0 ** (cent_offset / 1200.0))
            tid = build_test_id(note_label, cent_offset)
            wav_path = corpus_dir / f"{tid}_input.wav"
            mfst_path = corpus_dir / f"{tid}.manifest.json"
            write_wav_16bit(wav_path,
                            generate_stable_tone(actual_hz, SIGNAL_SAMPLE_RATE, 3.0),
                            SIGNAL_SAMPLE_RATE)
            # For boundary cases, target is the ORIGINAL note (same MIDI),
            # but we'll record what target the engine actually selects.
            m = {
                "testId":      tid,
                "suite":       "boundary",
                "noteLabel":   note_label,
                "nominalHz":   nominal_hz,
                "centOffset":  cent_offset,
                "actualHz":    round(actual_hz, 6),
                "nominalTargetMidi":  target_midi,
                "nominalTargetHz":    round(midi_to_hz(target_midi), 6),
                "durationSec": 3.0,
                "sampleRate":  SIGNAL_SAMPLE_RATE,
            }
            mfst_path.write_text(json.dumps(m, indent=2) + "\n")
            manifests.append(m)
            print(f"  boundary corpus: {tid}")
    return manifests


# ---------------------------------------------------------------------------
# Bench runner
# ---------------------------------------------------------------------------

def find_bench(repo_root: Path) -> Path | None:
    for p in [
        repo_root / "build" / "plugins" / "auto-pitch" / "AutoPitchBench",
        repo_root / "build" / "plugins" / "auto-pitch" / "Release" / "AutoPitchBench",
    ]:
        if p.exists() and os.access(p, os.X_OK):
            return p
    return None


def run_bench(bench: Path, manifest: dict, corpus_dir: Path,
              results_dir: Path, bench_args: dict) -> dict:
    tid       = manifest["testId"]
    input_wav = corpus_dir  / f"{tid}_input.wav"
    out_wav   = results_dir / f"{tid}_corrected.wav"
    trace_j   = results_dir / f"{tid}_trace.json"
    results_dir.mkdir(parents=True, exist_ok=True)

    cmd = [
        str(bench), str(input_wav),
        "--output", str(out_wav),
        "--trace",  str(trace_j),
        "--key",    bench_args["key"],
        "--scale",  bench_args["scale"],
        "--style",  str(bench_args["style"]),
        "--amount", str(bench_args["amount"]),
        "--speed",  str(bench_args["speed"]),
        "--block",  str(bench_args["block"]),
    ]

    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        if proc.returncode != 0:
            return {"testId": tid, "crash": True,
                    "crashReason": f"exit {proc.returncode}: {proc.stderr.strip()}"}
    except subprocess.TimeoutExpired:
        return {"testId": tid, "crash": True, "crashReason": "timeout"}
    except Exception as exc:
        return {"testId": tid, "crash": True, "crashReason": str(exc)}

    trace: dict = {}
    if trace_j.exists():
        try:
            trace = json.loads(trace_j.read_text())
        except Exception:
            pass

    return {
        "testId":                  tid,
        "crash":                   False,
        "outputWav":               str(out_wav),
        "traceJson":               str(trace_j),
        "latencySamples":          trace.get("latencySamples", -1),
        "engine":                  trace.get("engine", "unknown"),
        "avgTraceCorrectionCents": trace.get("avgTraceCorrectionCents", 0.0),
        "voicedBlocks":            trace.get("voicedBlocks", 0),
        "totalBlocks":             trace.get("totalBlocks", 1),
        "outputPeak":              trace.get("outputPeak", 0.0),
        "outputRms":               trace.get("outputRms", 0.0),
        "frames":                  trace.get("frames", []),
        "blockSize":               trace.get("blockSize", 512),
    }


# ---------------------------------------------------------------------------
# Scoring (stable + duration)
# ---------------------------------------------------------------------------

def score_case(manifest: dict, bench_result: dict,
               corpus_dir: Path) -> dict:
    tid        = manifest["testId"]
    actual_hz  = manifest["actualHz"]
    target_hz  = manifest.get("targetHz") or manifest.get("nominalTargetHz")
    target_midi = manifest.get("targetMidi") or manifest.get("nominalTargetMidi")
    cent_offset = manifest["centOffset"]
    needed_cents = -cent_offset

    result: dict = {
        "testId":      tid,
        "suite":       manifest.get("suite", "stable"),
        "noteLabel":   manifest["noteLabel"],
        "centOffset":  cent_offset,
        "durationSec": manifest["durationSec"],
        "nominalHz":   manifest["nominalHz"],
        "actualInputHz": round(actual_hz, 4),
        "targetHz":    round(target_hz, 4) if target_hz else None,
        "targetMidi":  target_midi,
        "neededCorrectionCents": round(needed_cents, 2),
    }

    if bench_result.get("crash"):
        result.update({
            "crash": True, "crashReason": bench_result.get("crashReason", ""),
            "finiteOutput": False, "nonSilentOutput": False,
            "targetNoteCorrect": False, "octaveError": False, "passed": False,
        })
        return result

    result["crash"]         = False
    result["engine"]        = bench_result.get("engine", "unknown")
    result["latencySamples"] = bench_result.get("latencySamples", -1)
    voiced  = bench_result.get("voicedBlocks", 0)
    total   = bench_result.get("totalBlocks", 1)
    result["voicedRatio"]  = voiced / max(1, total)
    result["avgTraceCorrectionCents"] = bench_result.get("avgTraceCorrectionCents", 0.0)
    result["outputPeak"]   = bench_result.get("outputPeak", 0.0)
    result["outputRms"]    = bench_result.get("outputRms", 0.0)

    # Independent ACF estimate — input
    in_hz, in_conf = 0.0, 0.0
    input_wav = corpus_dir / f"{tid}_input.wav"
    if input_wav.exists():
        try:
            in_s, in_sr = read_wav_mono(input_wav)
            in_hz, in_conf = estimate_pitch_acf(
                in_s, in_sr, min_hz=actual_hz * 0.8, max_hz=actual_hz * 1.25)
        except Exception as exc:
            result["inputEstimateError"] = str(exc)

    result["inputEstimatedHz"]  = round(in_hz, 4)
    result["inputEstimateConf"] = round(in_conf, 3)
    result["inputErrorCents"] = (round(cents_between(in_hz, actual_hz), 2)
                                  if in_hz > 0 else None)

    # Independent ACF estimate — output
    out_hz, out_conf = 0.0, 0.0
    out_wav = Path(bench_result.get("outputWav", ""))
    if out_wav.exists() and target_hz:
        try:
            out_s, out_sr = read_wav_mono(out_wav)
            out_hz, out_conf = estimate_pitch_acf(
                out_s, out_sr,
                min_hz=target_hz * 0.8, max_hz=target_hz * 1.25)
        except Exception as exc:
            result["outputEstimateError"] = str(exc)

    result["outputEstimatedHz"]  = round(out_hz, 4)
    result["outputEstimateConf"] = round(out_conf, 3)

    out_err = (cents_between(out_hz, target_hz)
               if out_hz > 0.0 and target_hz else None)
    result["outputErrorCents"] = round(out_err, 2) if out_err is not None else None

    # Correction efficiency
    if cent_offset != 0 and out_err is not None:
        eff = correction_efficiency(float(cent_offset), out_err)
        result["correctionEfficiency"] = round(eff, 4) if eff is not None else None
    else:
        result["correctionEfficiency"] = None

    # Sanity
    result["finiteOutput"]    = bench_result.get("outputPeak", 0.0) < float("inf")
    result["nonSilentOutput"] = bench_result.get("outputRms", 0.0) > 1.0e-4

    # Scoring
    abs_err = abs(out_err) if out_err is not None else float("inf")
    target_note_correct = abs_err < TARGET_CORRECT_THRESH_CENTS
    octave_error = OCTAVE_ERROR_LO_CENTS < abs_err < OCTAVE_ERROR_HI_CENTS

    result["targetNoteCorrect"] = target_note_correct
    result["octaveError"]       = octave_error

    # Convergence analysis from trace (Phase 3/4)
    frames = bench_result.get("frames", [])
    if frames and needed_cents != 0.0:
        conv = analyze_convergence(frames, needed_cents, manifest["durationSec"])
        result["convergence"] = conv

        root_cause = classify_root_cause(needed_cents, frames, out_err)
        result["rootCause"] = root_cause
    else:
        result["convergence"] = {}
        result["rootCause"] = "N/A (0c offset)"

    result["passed"] = (
        target_note_correct
        and not octave_error
        and result["finiteOutput"]
        and result["nonSilentOutput"]
        and not result["crash"]
    )
    return result


def score_boundary_case(manifest: dict, bench_result: dict) -> dict:
    """
    Boundary scoring: determine which target note the engine actually selected.
    """
    tid         = manifest["testId"]
    cent_offset = manifest["centOffset"]
    nominal_hz  = manifest["nominalHz"]
    nominal_midi = manifest["nominalTargetMidi"]
    actual_hz   = manifest["actualHz"]

    result: dict = {
        "testId":       tid,
        "suite":        "boundary",
        "noteLabel":    manifest["noteLabel"],
        "centOffset":   cent_offset,
        "actualInputHz": round(actual_hz, 4),
        "nominalTargetMidi": nominal_midi,
    }

    if bench_result.get("crash"):
        result.update({"crash": True, "crashReason": bench_result.get("crashReason", "")})
        return result

    result["crash"] = False
    result["avgTraceCorrectionCents"] = bench_result.get("avgTraceCorrectionCents", 0.0)
    result["voicedRatio"] = (bench_result.get("voicedBlocks", 0) /
                              max(1, bench_result.get("totalBlocks", 1)))

    frames = bench_result.get("frames", [])
    voiced = [f for f in frames if f.get("voiced")]

    # Determine the engine's selected target from steady-state trace frames
    ss_start = max(0, len(voiced) * 3 // 4)
    ss = voiced[ss_start:]

    if ss:
        ss_target_midis = [f.get("targetMidi", nominal_midi) for f in ss]
        avg_target_midi = sum(ss_target_midis) / len(ss_target_midis)
        result["engineTargetMidi"]   = round(avg_target_midi, 3)
        result["engineTargetNote"]   = round(avg_target_midi)
        result["targetMatchNominal"] = abs(avg_target_midi - nominal_midi) < 0.5

        ss_corr  = [f.get("correctionCents", 0.0) for f in ss]
        ss_conf  = [f.get("confidence", 0.0) for f in ss]
        result["avgSteadyStateCorrectionCents"] = round(
            sum(ss_corr) / len(ss_corr), 2)
        result["avgSteadyStateConfidence"] = round(
            sum(ss_conf) / len(ss_conf), 3)

        # Applied shift via smoothedPitchScale
        ss_sps = [scale_to_cents(f.get("smoothedPitchScale", 1.0)) for f in ss]
        result["avgSteadyStateAppliedCents"] = round(
            sum(ss_sps) / len(ss_sps), 2)
    else:
        result["engineTargetMidi"]   = None
        result["targetMatchNominal"] = None

    return result


# ---------------------------------------------------------------------------
# Statistics aggregation (Phase 2)
# ---------------------------------------------------------------------------

def aggregate_stats(scored: list[dict]) -> dict:
    """Full statistical breakdown for Phase 2."""
    def extract(cases, key):
        return [r[key] for r in cases if r.get(key) is not None]

    out_errors_abs = [abs(e) for e in extract(scored, "outputErrorCents")]
    out_errors     = extract(scored, "outputErrorCents")
    efficiencies   = extract(scored, "correctionEfficiency")

    summary: dict = {
        "totalCases": len(scored),
        "passed": sum(1 for r in scored if r.get("passed")),
        "crashes": sum(1 for r in scored if r.get("crash")),
        "octaveErrors": sum(1 for r in scored if r.get("octaveError")),
        "silentOutputs": sum(1 for r in scored if not r.get("nonSilentOutput", True)),
        "absOutputErrorStats": compute_stats(out_errors_abs),
        "signedOutputErrorStats": compute_stats(out_errors),
        "correctionEfficiencyStats": compute_stats(efficiencies),
    }

    # --- By register ---
    by_register: dict = {}
    for note_label, _, _ in TEST_NOTES:
        cases_n = [r for r in scored if r.get("noteLabel") == note_label]
        errs_n  = [abs(e) for e in extract(cases_n, "outputErrorCents")]
        eff_n   = extract(cases_n, "correctionEfficiency")
        by_register[note_label] = {
            "n": len(cases_n),
            "passed": sum(1 for r in cases_n if r.get("passed")),
            "absErrorStats": compute_stats(errs_n),
            "efficiencyStats": compute_stats(eff_n),
        }
    summary["byRegister"] = by_register

    # --- By magnitude ---
    magnitude_bands = [
        ("0-10c",  0,  10),
        ("11-20c", 11, 20),
        ("21-30c", 21, 30),
        ("31-40c", 31, 40),
        ("41-49c", 41, 59),
    ]
    by_magnitude: dict = {}
    for band_name, lo, hi in magnitude_bands:
        cases_b = [r for r in scored
                   if lo <= abs(r.get("centOffset", 0)) <= hi]
        errs_b  = [abs(e) for e in extract(cases_b, "outputErrorCents")]
        eff_b   = extract(cases_b, "correctionEfficiency")
        by_magnitude[band_name] = {
            "n": len(cases_b),
            "passed": sum(1 for r in cases_b if r.get("passed")),
            "absErrorStats": compute_stats(errs_b),
            "efficiencyStats": compute_stats(eff_b),
        }
    summary["byMagnitude"] = by_magnitude

    # --- By direction ---
    flat_cases  = [r for r in scored if r.get("centOffset", 0) < 0]
    sharp_cases = [r for r in scored if r.get("centOffset", 0) > 0]
    on_pitch    = [r for r in scored if r.get("centOffset", 0) == 0]
    by_direction: dict = {}
    for label, cases_d in [("flat", flat_cases), ("sharp", sharp_cases),
                            ("on_pitch", on_pitch)]:
        errs_d = [abs(e) for e in extract(cases_d, "outputErrorCents")]
        eff_d  = extract(cases_d, "correctionEfficiency")
        by_direction[label] = {
            "n": len(cases_d),
            "passed": sum(1 for r in cases_d if r.get("passed")),
            "absErrorStats": compute_stats(errs_d),
            "efficiencyStats": compute_stats(eff_d),
        }
    summary["byDirection"] = by_direction

    # --- Convergence summary (from Phase 3 duration data) ---
    time_to_10 = [r["convergence"]["timeTo10c"]
                  for r in scored
                  if r.get("convergence", {}).get("timeTo10c") is not None]
    time_to_5  = [r["convergence"]["timeTo5c"]
                  for r in scored
                  if r.get("convergence", {}).get("timeTo5c") is not None]
    ss_residuals = [r["convergence"]["steadyStateResidualCents"]
                    for r in scored
                    if r.get("convergence", {}).get("steadyStateResidualCents") is not None]
    if time_to_10:
        summary["timeTo10cStats"] = compute_stats(time_to_10)
    if time_to_5:
        summary["timeTo5cStats"] = compute_stats(time_to_5)
    if ss_residuals:
        summary["steadyStateResidualStats"] = compute_stats(
            [abs(x) for x in ss_residuals])

    # --- Root cause breakdown ---
    rc_counts: dict[str, int] = {}
    for r in scored:
        rc = r.get("rootCause", "UNKNOWN")
        rc_counts[rc] = rc_counts.get(rc, 0) + 1
    summary["rootCauseCounts"] = rc_counts

    return summary


# ---------------------------------------------------------------------------
# Boundary analysis (Phase 5)
# ---------------------------------------------------------------------------

def analyze_boundary(scored_boundary: list[dict]) -> dict:
    """Build semitone boundary table from boundary corpus results."""
    rows: list[dict] = []
    for r in scored_boundary:
        if r.get("crash"):
            continue
        rows.append({
            "testId":         r["testId"],
            "noteLabel":      r["noteLabel"],
            "centOffset":     r["centOffset"],
            "engineTarget":   r.get("engineTargetMidi"),
            "targetMatch":    r.get("targetMatchNominal"),
            "corrCents":      r.get("avgSteadyStateCorrectionCents"),
            "appliedCents":   r.get("avgSteadyStateAppliedCents"),
            "confidence":     r.get("avgSteadyStateConfidence"),
        })
    rows.sort(key=lambda x: (x["noteLabel"], x["centOffset"]))

    # Find flip points (where target changes from nominal to neighbor)
    flip_points: list[dict] = []
    for note_label in [n[0] for n in TEST_NOTES]:
        note_rows = [r for r in rows if r["noteLabel"] == note_label]
        note_rows.sort(key=lambda x: x["centOffset"])
        prev = None
        for row in note_rows:
            if prev and prev.get("targetMatch") != row.get("targetMatch"):
                flip_points.append({
                    "noteLabel": note_label,
                    "flipBetween": [prev["centOffset"], row["centOffset"]],
                    "prevTarget": prev.get("engineTarget"),
                    "newTarget":  row.get("engineTarget"),
                })
            prev = row

    return {"rows": rows, "flipPoints": flip_points}


# ---------------------------------------------------------------------------
# Report generation
# ---------------------------------------------------------------------------

def _stat_str(s: dict | None) -> str:
    if not s:
        return "—"
    return (f"mean={s.get('mean')}c  med={s.get('median')}c  "
            f"p90={s.get('p90')}c  p95={s.get('p95')}c  max={s.get('max')}c")


def write_stable_report(scored: list[dict], summary: dict,
                         output_dir: Path, bench_args: dict,
                         bench_path: str) -> None:
    results_doc = {
        "version":       "2.0",
        "benchBinary":   bench_path,
        "benchSettings": bench_args,
        "summary":       summary,
        "cases":         scored,
    }
    rj = output_dir / "results.json"
    rj.write_text(json.dumps(results_doc, indent=2) + "\n")
    print(f"\nwrote: {rj}")

    lines: list[str] = []
    lines.append("# VocalBench v2 — Auto Pitch Stable Corpus\n")
    lines.append(f"Key: {bench_args['key']} {bench_args['scale']}  "
                 f"Style: {bench_args['style']}  Amount: {bench_args['amount']}  "
                 f"Speed: {bench_args['speed']}  Block: {bench_args['block']}\n")

    s  = summary
    total  = s["totalCases"]
    passed = s["passed"]
    lines.append("## Summary\n")
    lines.append(f"| | |")
    lines.append(f"|---|---|")
    lines.append(f"| Total / Passed | {passed} / {total} |")
    lines.append(f"| Crashes | {s['crashes']} |")
    lines.append(f"| Octave errors | {s['octaveErrors']} |")
    lines.append(f"| Abs output error | {_stat_str(s.get('absOutputErrorStats'))} |")
    lines.append(f"| Correction efficiency | {_stat_str(s.get('correctionEfficiencyStats'))} |")

    lines.append("\n## By Register\n")
    lines.append("| Register | N | Pass | Mean Err | p90 | p95 | Eff mean |")
    lines.append("|----------|---|------|----------|-----|-----|----------|")
    for note in [n[0] for n in TEST_NOTES]:
        b = s["byRegister"].get(note, {})
        ae = b.get("absErrorStats", {})
        ef = b.get("efficiencyStats", {})
        lines.append(f"| {note} | {b.get('n','—')} | {b.get('passed','—')} | "
                     f"{ae.get('mean','—')}c | {ae.get('p90','—')}c | "
                     f"{ae.get('p95','—')}c | {ef.get('mean','—')} |")

    lines.append("\n## By Input Magnitude\n")
    lines.append("| Band | N | Pass | Mean Err | p90 | Eff mean |")
    lines.append("|------|---|------|----------|-----|----------|")
    for band_name, _, _ in [("0-10c", 0, 10), ("11-20c", 11, 20),
                              ("21-30c", 21, 30), ("31-40c", 31, 40),
                              ("41-49c", 41, 59)]:
        b = s["byMagnitude"].get(band_name, {})
        ae = b.get("absErrorStats", {})
        ef = b.get("efficiencyStats", {})
        lines.append(f"| {band_name} | {b.get('n','—')} | {b.get('passed','—')} | "
                     f"{ae.get('mean','—')}c | {ae.get('p90','—')}c | "
                     f"{ef.get('mean','—')} |")

    lines.append("\n## By Direction\n")
    lines.append("| Direction | N | Pass | Mean Err | Eff mean |")
    lines.append("|-----------|---|------|----------|----------|")
    for direction in ["flat", "sharp", "on_pitch"]:
        b = s["byDirection"].get(direction, {})
        ae = b.get("absErrorStats", {})
        ef = b.get("efficiencyStats", {})
        lines.append(f"| {direction} | {b.get('n','—')} | {b.get('passed','—')} | "
                     f"{ae.get('mean','—')}c | {ef.get('mean','—')} |")

    # Root cause breakdown
    rc = s.get("rootCauseCounts", {})
    if rc:
        lines.append("\n## Root Cause Classification\n")
        for cause, count in sorted(rc.items(), key=lambda x: -x[1]):
            lines.append(f"- {cause}: {count}")

    # Per-note table
    for note_label, _, _ in TEST_NOTES:
        lines.append(f"\n### {note_label}\n")
        lines.append("| Offset | In Est Hz | In Err | "
                     "Out Est Hz | Out Err | Eff | "
                     "SS Residual | Time→10c | Root Cause | Pass |")
        lines.append("|--------|-----------|--------|"
                     "-----------|---------|-----|"
                     "------------|----------|------------|------|")
        for r in scored:
            if r.get("noteLabel") != note_label:
                continue
            off_str = f"{r['centOffset']:+.0f}c"
            in_hz   = f"{r.get('inputEstimatedHz',0):.1f}" if r.get('inputEstimatedHz') else "—"
            in_err  = (f"{r['inputErrorCents']:+.1f}c"
                       if r.get("inputErrorCents") is not None else "—")
            out_hz  = f"{r.get('outputEstimatedHz',0):.1f}" if r.get('outputEstimatedHz') else "—"
            out_err = (f"{r['outputErrorCents']:+.1f}c"
                       if r.get("outputErrorCents") is not None else "—")
            eff_str = (f"{r['correctionEfficiency']:.2f}"
                       if r.get("correctionEfficiency") is not None else "—")
            conv    = r.get("convergence", {})
            ss_res  = conv.get("steadyStateResidualCents")
            ss_str  = f"{ss_res:+.1f}c" if ss_res is not None else "—"
            t10     = conv.get("timeTo10c")
            t10_str = f"{t10:.3f}s" if t10 is not None else "NR"
            rc_str  = r.get("rootCause", "—")
            pass_str = "✅" if r.get("passed") else "❌"
            if r.get("crash"):
                lines.append(f"| {off_str} | CRASH | — | — | — | — | — | — | — | ❌ |")
            else:
                lines.append(f"| {off_str} | {in_hz} | {in_err} | "
                              f"{out_hz} | {out_err} | {eff_str} | "
                              f"{ss_str} | {t10_str} | {rc_str} | {pass_str} |")

    report_md = output_dir / "report.md"
    report_md.write_text("\n".join(lines) + "\n")
    print(f"wrote: {report_md}")


def write_duration_report(scored: list[dict], output_dir: Path) -> None:
    lines: list[str] = []
    lines.append("# VocalBench v2 — Duration / Time-to-Target\n")

    lines.append("## Time-to-±10c by Note and Offset (smoothedPitchScale criterion)\n")
    lines.append("| Note | Offset | 0.25s | 0.50s | 1.0s | 2.0s | 4.0s | 8.0s |")
    lines.append("|------|--------|-------|-------|------|------|------|------|")

    for note_label, _, _ in TEST_NOTES:
        for off in DURATION_OFFSETS:
            row_cells = [f"A{note_label[-1]}" if off == DURATION_OFFSETS[0] else "",
                         f"{off:+d}c"]
            for dur in DURATION_SECS:
                dur_str = f"{dur:.2f}s".replace(".", "p")
                tid = build_test_id(note_label, off, dur_str)
                case = next((r for r in scored if r["testId"] == tid), None)
                if case is None:
                    row_cells.append("—")
                    continue
                conv = case.get("convergence", {})
                t10  = conv.get("timeTo10c")
                if t10 is None:
                    row_cells.append("NR")
                elif t10 > dur:
                    row_cells.append("NR")
                else:
                    row_cells.append(f"{t10:.2f}s")
            lines.append("| " + " | ".join(str(c) for c in row_cells) + " |")

    lines.append("\n## Steady-State Residual by Note and Offset\n")
    lines.append("| Note | Offset | 8s SS Residual | Root Cause |")
    lines.append("|------|--------|----------------|------------|")
    for note_label, _, _ in TEST_NOTES:
        for off in DURATION_OFFSETS:
            dur_str = "8p00s"
            tid = build_test_id(note_label, off, dur_str)
            case = next((r for r in scored if r["testId"] == tid), None)
            if case is None:
                lines.append(f"| {note_label} | {off:+d}c | — | — |")
                continue
            conv = case.get("convergence", {})
            ss   = conv.get("steadyStateResidualCents")
            rc   = case.get("rootCause", "—")
            ss_str = f"{ss:+.2f}c" if ss is not None else "—"
            lines.append(f"| {note_label} | {off:+d}c | {ss_str} | {rc} |")

    report_md = output_dir / "duration_report.md"
    report_md.write_text("\n".join(lines) + "\n")
    print(f"wrote: {report_md}")

    # JSON
    rj = output_dir / "duration_results.json"
    rj.write_text(json.dumps({"cases": scored}, indent=2) + "\n")
    print(f"wrote: {rj}")


def write_boundary_report(scored_boundary: list[dict],
                           analysis: dict, output_dir: Path) -> None:
    lines: list[str] = []
    lines.append("# VocalBench v2 — Semitone Decision Boundary (Phase 5)\n")

    lines.append("## Per-Register Boundary Table\n")
    for note_label, _, _ in TEST_NOTES:
        lines.append(f"\n### {note_label}\n")
        lines.append("| Offset | Engine Target MIDI | Match Nominal | "
                     "SS Corr Req | SS Applied | SS Conf |")
        lines.append("|--------|-------------------|---------------|"
                     "-------------|------------|---------|")
        rows = [r for r in analysis["rows"] if r["noteLabel"] == note_label]
        for r in rows:
            match_str = "✅" if r.get("targetMatch") else "❌"
            lines.append(
                f"| {r['centOffset']:+.1f}c | {r.get('engineTarget','—')} | {match_str} | "
                f"{r.get('corrCents','—')}c | {r.get('appliedCents','—')}c | "
                f"{r.get('confidence','—')} |")

    if analysis["flipPoints"]:
        lines.append("\n## Flip Points (target-note transitions)\n")
        for fp in analysis["flipPoints"]:
            lines.append(f"- {fp['noteLabel']}: between "
                          f"{fp['flipBetween'][0]:+.1f}c and {fp['flipBetween'][1]:+.1f}c "
                          f"(MIDI {fp.get('prevTarget','?')} → {fp.get('newTarget','?')})")

    report_md = output_dir / "boundary_report.md"
    report_md.write_text("\n".join(lines) + "\n")
    print(f"wrote: {report_md}")

    rj = output_dir / "boundary_results.json"
    rj.write_text(json.dumps({"rows": analysis["rows"],
                               "flipPoints": analysis["flipPoints"]},
                              indent=2) + "\n")
    print(f"wrote: {rj}")


def print_final_summary(summary: dict, suite: str) -> None:
    s      = summary
    total  = s["totalCases"]
    passed = s["passed"]
    ae     = s.get("absOutputErrorStats", {})
    print(f"\n{'='*65}")
    print(f"VocalBench v2 [{suite}] — {passed}/{total} passed")
    if ae:
        print(f"  abs output error:  mean={ae.get('mean')}c  "
              f"p90={ae.get('p90')}c  p95={ae.get('p95')}c")
    rc = s.get("rootCauseCounts", {})
    if rc:
        print(f"  root causes:       {rc}")
    print(f"  octave errors:     {s['octaveErrors']}")
    print(f"  crashes:           {s['crashes']}")
    print(f"{'='*65}")


# ---------------------------------------------------------------------------
# Regression test runner
# ---------------------------------------------------------------------------

def run_regression_tests(repo_root: Path, bench_args: dict) -> list[dict]:
    tests = []
    build = repo_root / "build" / "plugins" / "auto-pitch"

    # CoreContract
    r = _run_cmd([str(build / "AutoPitchCoreContract")], "AutoPitchCoreContract")
    tests.append(r)
    print(f"  {'✅' if r['passed'] else '❌'} AutoPitchCoreContract")

    # Render smoke
    r = _run_cmd([str(build / "AutoPitchRender"), "--smoke"], "AutoPitchRender --smoke")
    tests.append(r)
    print(f"  {'✅' if r['passed'] else '❌'} AutoPitchRender --smoke")

    # Render null-test
    r = _run_cmd([str(build / "AutoPitchRender")], "AutoPitchRender null-test",
                 success_phrase="null-test ok")
    tests.append(r)
    print(f"  {'✅' if r['passed'] else '❌'} AutoPitchRender null-test")

    return tests


def _run_cmd(cmd: list[str], name: str, success_phrase: str = "") -> dict:
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        out = proc.stdout + proc.stderr
        ok = proc.returncode == 0
        if success_phrase:
            ok = ok and success_phrase in out
        return {"name": name, "passed": ok, "exitCode": proc.returncode,
                "output": out.strip()}
    except Exception as exc:
        return {"name": name, "passed": False, "exitCode": -1, "output": str(exc)}


# ---------------------------------------------------------------------------
# Main pipeline
# ---------------------------------------------------------------------------

def run_suite(suite: str, output_dir: Path, bench: Path,
              bench_args: dict, dry_run: bool) -> int:
    do_stable   = suite in ("stable", "full", "nightly", "research")
    do_duration = suite in ("duration", "full", "nightly")
    do_boundary = suite in ("boundary", "full", "research")

    output_dir.mkdir(parents=True, exist_ok=True)
    total_pass = total_cases = 0

    # --- Stable ---
    if do_stable:
        print(f"\n[stable] Generating corpus")
        corpus_dir  = output_dir / "corpus" / "stable"
        results_dir = output_dir / "results" / "stable"
        manifests   = generate_stable_corpus(corpus_dir)
        if not dry_run:
            print(f"\n[stable] Processing {len(manifests)} cases")
            bench_results = [run_bench(bench, m, corpus_dir, results_dir, bench_args)
                             for m in manifests]
            print(f"\n[stable] Scoring")
            scored = [score_case(m, br, corpus_dir)
                      for m, br in zip(manifests, bench_results)]
            for r in scored:
                mark = "✅" if r.get("passed") else "❌"
                oe   = r.get("outputErrorCents")
                err  = f"{oe:+.1f}c" if oe is not None else "—"
                rc   = r.get("rootCause", "")
                print(f"  {mark} {r['testId']:36s}  {err:>8}  {rc}")
            summary = aggregate_stats(scored)
            print(f"\n[stable] Writing results")
            write_stable_report(scored, summary, output_dir, bench_args, str(bench))
            print_final_summary(summary, "stable")
            total_pass  += summary["passed"]
            total_cases += summary["totalCases"]

    # --- Duration ---
    if do_duration:
        print(f"\n[duration] Generating corpus")
        corpus_dir  = output_dir / "corpus" / "duration"
        results_dir = output_dir / "results" / "duration"
        manifests   = generate_duration_corpus(corpus_dir)
        if not dry_run:
            print(f"\n[duration] Processing {len(manifests)} cases")
            bench_results = [run_bench(bench, m, corpus_dir, results_dir, bench_args)
                             for m in manifests]
            print(f"\n[duration] Scoring")
            scored = [score_case(m, br, corpus_dir)
                      for m, br in zip(manifests, bench_results)]
            for r in scored:
                conv = r.get("convergence", {})
                t10  = conv.get("timeTo10c")
                t10s = f"→10c:{t10:.2f}s" if t10 else "→10c:NR"
                rc   = r.get("rootCause", "")
                print(f"  {r['testId']:46s}  {t10s}  {rc}")
            write_duration_report(scored, output_dir)
            pass_d  = sum(1 for r in scored if r.get("passed"))
            total_pass  += pass_d
            total_cases += len(scored)

    # --- Boundary ---
    if do_boundary:
        print(f"\n[boundary] Generating corpus")
        corpus_dir  = output_dir / "corpus" / "boundary"
        results_dir = output_dir / "results" / "boundary"
        manifests   = generate_boundary_corpus(corpus_dir)
        if not dry_run:
            print(f"\n[boundary] Processing {len(manifests)} cases")
            bench_results = [run_bench(bench, m, corpus_dir, results_dir, bench_args)
                             for m in manifests]
            print(f"\n[boundary] Scoring")
            scored_b = [score_boundary_case(m, br)
                        for m, br in zip(manifests, bench_results)]
            for r in scored_b:
                match = "✅" if r.get("targetMatchNominal") else "❌"
                tgt   = r.get("engineTargetMidi", "?")
                corr  = r.get("avgSteadyStateCorrectionCents", "?")
                print(f"  {match} {r['testId']:40s}  tgt={tgt}  corr={corr}c")
            analysis = analyze_boundary(scored_b)
            write_boundary_report(scored_b, analysis, output_dir)
            if analysis["flipPoints"]:
                print(f"\n  Boundary flip points:")
                for fp in analysis["flipPoints"]:
                    print(f"    {fp['noteLabel']}: between "
                          f"{fp['flipBetween'][0]:+.1f}c and {fp['flipBetween'][1]:+.1f}c")

    if not dry_run and total_cases > 0:
        status = 0 if total_pass == total_cases else 1
        print(f"\nTotal: {total_pass}/{total_cases} passed  →  exit {status}")
        return status
    return 0


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(
        description="VocalBench v2 — Auto Pitch Engine Research Laboratory")
    ap.add_argument("--bench",  default="",
                    help="Path to AutoPitchBench binary")
    ap.add_argument("--suite",  default="stable",
                    choices=["stable", "duration", "boundary", "full",
                             "nightly", "research"],
                    help="Which test suite to run (default: stable)")
    ap.add_argument("--output", default="vocalbench-results",
                    help="Output directory (default: vocalbench-results)")
    ap.add_argument("--key",    default="A")
    ap.add_argument("--scale",  default="minor")
    ap.add_argument("--style",  default=0, type=int)
    ap.add_argument("--amount", default=1.0, type=float)
    ap.add_argument("--speed",  default=0.9, type=float)
    ap.add_argument("--block",  default=512, type=int)
    ap.add_argument("--dry-run", action="store_true",
                    help="Generate corpus only; do not run bench or score")
    ap.add_argument("--skip-regression", action="store_true",
                    help="Skip regression test gate")
    args = ap.parse_args()

    output_dir = REPO_ROOT / args.output
    bench_args = {
        "key":    args.key,
        "scale":  args.scale,
        "style":  args.style,
        "amount": args.amount,
        "speed":  args.speed,
        "block":  args.block,
    }

    if args.dry_run:
        return run_suite(args.suite, output_dir, Path(""), bench_args, dry_run=True)

    bench: Path | None = Path(args.bench) if args.bench else find_bench(REPO_ROOT)
    if bench is None or not bench.exists() or not os.access(bench, os.X_OK):
        print("ERROR: AutoPitchBench binary not found.", file=sys.stderr)
        print("  Build: cmake --build build --target AutoPitchBench", file=sys.stderr)
        return 1

    if not args.skip_regression:
        print("\n[0/4] Regression gate")
        reg_tests = run_regression_tests(REPO_ROOT, bench_args)
        failures  = [t for t in reg_tests if not t["passed"]]
        if failures:
            print(f"  REGRESSION GATE FAILED: {[t['name'] for t in failures]}")
            return 2

    return run_suite(args.suite, output_dir, bench, bench_args, dry_run=False)


if __name__ == "__main__":
    raise SystemExit(main())
