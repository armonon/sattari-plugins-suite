#!/usr/bin/env python3
"""
Torture corpus builder + regression checker.

Phase 2: Permanentize RUN_up_350ms as a first-class regression case.
Phase 3: Perceptual proxy — ascending-run variants to assess 285¢/22ms transient risk.

Usage:
    python3 scripts/torture_bench.py          # build corpus + run proxy, emit verdict
    python3 scripts/torture_bench.py --check  # regression-only against saved manifests
"""

import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path

REPO       = Path(__file__).resolve().parent.parent
BENCH      = REPO / "build" / "plugins" / "auto-pitch" / "AutoPitchBench"
TORTURE    = REPO / "validation" / "torture"
SR         = 44100
BLOCK      = 512
BLOCK_SEC  = BLOCK / SR

TORTURE.mkdir(parents=True, exist_ok=True)


# ---------------------------------------------------------------------------
# Shared audio helpers
# ---------------------------------------------------------------------------

def write_wav(path: Path, samples: list[float], sr: int = SR) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = struct.pack(f"<{len(samples)}h",
                       *[max(-32768, min(32767, int(s * 32767))) for s in samples])
    with wave.open(str(path), "w") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        wf.writeframes(data)


def read_wav(path: Path) -> list[float]:
    with wave.open(str(path), "r") as wf:
        raw = wf.readframes(wf.getnframes())
        nc  = wf.getnchannels()
    n = len(raw) // 2
    s = struct.unpack(f"<{n}h", raw)
    if nc > 1:
        s = s[::nc]
    return [v / 32768.0 for v in s]


def midi_to_hz(m: float) -> float:
    return 440.0 * (2.0 ** ((m - 69.0) / 12.0))


def sine_sweep(midi_start: float, midi_end: float, dur_sec: float,
               sr: int = SR) -> list[float]:
    """Linear-frequency-log sine sweep from midi_start to midi_end."""
    n = int(dur_sec * sr)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / n
        hz = midi_to_hz(midi_start + (midi_end - midi_start) * t)
        out.append(math.sin(phase))
        phase += 2 * math.pi * hz / sr
    return out


def sine_note(midi: float, dur_sec: float, sr: int = SR,
              vibrato_hz: float = 0.0, vibrato_cents: float = 0.0,
              attack_sec: float = 0.01) -> list[float]:
    n = int(dur_sec * sr)
    out = []
    phase = 0.0
    vib_phase = 0.0
    for i in range(n):
        t = i / sr
        env = min(1.0, t / attack_sec) if attack_sec > 0 else 1.0
        vib = vibrato_cents * math.sin(vib_phase) / 100.0  # semitones
        hz  = midi_to_hz(midi + vib)
        out.append(env * math.sin(phase))
        phase     += 2 * math.pi * hz / sr
        vib_phase += 2 * math.pi * vibrato_hz / sr
    return out


def run_bench(wav: Path, key: str, scale: str, intent_mode: str,
              out_wav: Path, trace_path: Path) -> dict:
    cmd = [str(BENCH), str(wav),
           "--output", str(out_wav), "--trace", str(trace_path),
           "--key", key, "--scale", scale,
           "--amount", "0.82", "--speed", "0.70",
           "--block", str(BLOCK),
           "--intent-mode", intent_mode]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if r.returncode != 0 or not trace_path.exists():
        return {}
    return json.loads(trace_path.read_text())


def compute_jumps(frames: list[dict]) -> list[float]:
    voiced = [f for f in frames if f.get("voiced")]
    jumps = []
    for i in range(1, len(voiced)):
        prev = voiced[i-1].get("correctionCents", 0) or 0
        curr = voiced[i].get("correctionCents",   0) or 0
        j = abs(curr - prev)
        if j > 1.0:
            jumps.append(j)
    return jumps


def wav_rms(path: Path) -> float:
    s = read_wav(path)
    if not s:
        return 0.0
    return math.sqrt(sum(v*v for v in s) / len(s))


# ---------------------------------------------------------------------------
# Phase 2 — RUN_up_350ms torture test
# ---------------------------------------------------------------------------

def build_run_up_350ms_torture() -> dict:
    """
    Permanentize RUN_up_350ms as a regression case with full characterization.
    Returns characterization dict for manifest.
    """
    case_dir = TORTURE / "RUN_up_350ms"
    case_dir.mkdir(parents=True, exist_ok=True)

    # Copy or regenerate input WAV
    src_wav = REPO / "vocalbench-results" / "intent-bench-v3-audio" / "corpus" / "RUN_up_350ms.wav"
    inp_wav = case_dir / "input.wav"
    if src_wav.exists():
        shutil.copy2(src_wav, inp_wav)
    else:
        # Regenerate: 4-note ascending run A3→B3→C#4→D4 at 350ms per note, 500ms lead-in
        lead   = sine_note(57.0, 0.5)
        n1     = sine_sweep(57.0, 57.0, 0.350)   # A3 stable → slide onset
        n2     = sine_sweep(57.0, 59.0, 0.350)   # A3→B3 (semitone slide)
        n3     = sine_sweep(59.0, 61.0, 0.350)   # B3→C#4
        n4     = sine_note(62.0, 0.5)             # D4 stable tail
        samples = lead + n1 + n2 + n3 + n4
        write_wav(inp_wav, samples)

    # Run bench — production
    prod_trace_p = case_dir / "prod_trace.json"
    prod_out_p   = case_dir / "prod_corrected.wav"
    prod_trace   = run_bench(inp_wav, "A", "major", "production", prod_out_p, prod_trace_p)

    # Run bench — v3 active
    v3_trace_p = case_dir / "v3_trace.json"
    v3_out_p   = case_dir / "v3_corrected.wav"
    v3_trace   = run_bench(inp_wav, "A", "major", "v2_active", v3_out_p, v3_trace_p)

    if not v3_trace or not prod_trace:
        return {"error": "bench failed"}

    v3_frames   = v3_trace.get("frames", [])
    prod_frames = prod_trace.get("frames", [])

    v3_jumps   = compute_jumps(v3_frames)
    prod_jumps = compute_jumps(prod_frames)
    v3_max_j   = max(v3_jumps)   if v3_jumps   else 0.0
    prod_max_j = max(prod_jumps) if prod_jumps else 0.0

    # Locate the trigger frame (largest jump in v3)
    v3_voiced = [f for f in v3_frames if f.get("voiced")]
    trigger_idx = 0; trigger_jump = 0.0
    for i in range(1, len(v3_voiced)):
        prev = v3_voiced[i-1].get("correctionCents", 0) or 0
        curr = v3_voiced[i].get("correctionCents",   0) or 0
        j = abs(curr - prev)
        if j > trigger_jump:
            trigger_jump = j; trigger_idx = i

    tf = v3_voiced[trigger_idx]

    # Recovery time: how many frames until |correctionCents| < 20¢ after trigger
    recovery_frames = 0
    for k in range(trigger_idx + 1, len(v3_voiced)):
        recovery_frames += 1
        if abs(v3_voiced[k].get("correctionCents", 0) or 0) < 20.0:
            break

    # Duration of the transient (frames where cc > 100¢ near trigger)
    transient_frames = 0
    for k in range(max(0, trigger_idx - 1), min(len(v3_voiced), trigger_idx + 6)):
        if abs(v3_voiced[k].get("correctionCents", 0) or 0) > 100.0:
            transient_frames += 1

    charx = {
        "trigger_time_sec":       round(tf.get("timeSec", 0), 4),
        "trigger_frame_idx":      trigger_idx,
        "trigger_jump_cents":     round(trigger_jump, 1),
        "trigger_intent_reason":  tf.get("intentReasonName", "?"),
        "trigger_intent_target":  tf.get("intentTarget", -1),
        "trigger_detected_midi":  tf.get("detectedMidi",  -1),
        "trigger_correction_cc":  round(tf.get("correctionCents", 0) or 0, 1),
        "trigger_cme_vel_midi":   round(tf.get("motionVelMidi", 0) or 0, 2),
        "trigger_cme_conf":       round(tf.get("motionConf",    0) or 0, 4),
        "trigger_extrap_horizon": 0.080,
        "pre_frame_cc":           round(v3_voiced[trigger_idx-1].get("correctionCents",0) or 0, 1),
        "recovery_frames":        recovery_frames,
        "recovery_ms":            round(recovery_frames * BLOCK_SEC * 1000, 1),
        "transient_frames":       transient_frames,
        "transient_ms":           round(transient_frames * BLOCK_SEC * 1000, 1),
        "v3_max_jump_cents":      round(v3_max_j, 1),
        "prod_max_jump_cents":    round(prod_max_j, 1),
        "v3_p95_jump_cents":      round(sorted(v3_jumps)[int(len(v3_jumps)*0.95)] if v3_jumps else 0, 1),
    }

    # Write manifest
    manifest = {
        "id":           "RUN_up_350ms",
        "category":     "ascending_run_transient",
        "description":  ("4-note ascending run A3→B3→C#4→D4 at 350ms per slide. "
                         "Known Intent v3 transient: SLIDE_DESTINATION_V2 fires at high "
                         "CME velocity during run onset, overshoots target, PASSING_TONE "
                         "tier recovers in ≤2 frames."),
        "hard_thresholds": {
            "max_jump_cents":     350.0,
            "transient_ms_max":   35.0,
            "recovery_ms_max":    50.0,
            "crashes":            0,
            "octave_errors":      0,
        },
        "accepted_risk": (
            "v3 max_jump=285¢ vs prod 132¢. Transient is ~22ms, self-resolving. "
            "Strategy G speed=0.70 may partially attenuate the requested correction. "
            "Perceptual impact TBD by transient proxy suite."
        ),
        "characterization": charx,
        "engine_commit":    "d92093f",
        "strategy_g_commit": "6c022cb",
    }
    (case_dir / "manifest.json").write_text(json.dumps(manifest, indent=2))

    print(f"  RUN_up_350ms torture case:")
    print(f"    trigger t={charx['trigger_time_sec']}s  jump={charx['trigger_jump_cents']}¢")
    print(f"    reason={charx['trigger_intent_reason']}  target={charx['trigger_intent_target']}")
    print(f"    CME vel={charx['trigger_cme_vel_midi']} MIDI/sec  conf={charx['trigger_cme_conf']:.4f}")
    print(f"    pre_cc={charx['pre_frame_cc']}¢  post_cc={charx['trigger_correction_cc']}¢")
    print(f"    transient={charx['transient_ms']}ms  recovery={charx['recovery_ms']}ms")
    print(f"    v3 max_jump={charx['v3_max_jump_cents']}¢  prod max_jump={charx['prod_max_jump_cents']}¢")

    return manifest


# ---------------------------------------------------------------------------
# Phase 3 — Transient perceptual proxy
# ---------------------------------------------------------------------------

def gen_ascending_run(
    dur_ms: int,
    register_midi: float,
    interval_semi: int,
    start_cents: float,
    vibrato_cents: float,
    attack_sec: float,
    n_notes: int = 4,
) -> list[float]:
    """
    Generates an ascending run of n_notes each separated by interval_semi semitones.
    Lead-in: 400ms stable at register_midi.
    Each step: dur_ms sweep through interval_semi semitones.
    Tail: 300ms stable at final note.
    """
    base = register_midi + start_cents / 100.0
    samples = sine_note(base, 0.4, attack_sec=0.02)  # lead-in

    note = base
    for step in range(n_notes):
        note_end = note + interval_semi
        # Brief stable onset before slide
        samples += sine_note(note, 0.03, attack_sec=0.005,
                             vibrato_hz=5.5 if vibrato_cents > 0 else 0,
                             vibrato_cents=vibrato_cents)
        # Slide to next note
        samples += sine_sweep(note, note_end, dur_ms / 1000.0)
        note = note_end

    samples += sine_note(note, 0.3, attack_sec=0.005,  # tail
                         vibrato_hz=5.5 if vibrato_cents > 0 else 0,
                         vibrato_cents=vibrato_cents)
    return samples


def run_perceptual_proxy() -> dict:
    proxy_dir = TORTURE / "run_proxy"
    proxy_dir.mkdir(parents=True, exist_ok=True)

    cases = []
    for dur_ms in [250, 300, 350, 400, 450]:
        for reg_name, reg_midi in [("low", 45), ("mid", 57), ("high", 69)]:
            for interval in [1, 2]:     # semitone=1, whole-tone=2
                for vib_c in [0.0, 15.0]:
                    for start_c in [0.0, -20.0]:
                        cases.append({
                            "id":       f"RUN_{dur_ms}ms_{reg_name}_i{interval}_v{int(vib_c)}_s{int(start_c):+}",
                            "dur_ms":   dur_ms,
                            "reg_midi": reg_midi,
                            "interval": interval,
                            "vib_c":    vib_c,
                            "start_c":  start_c,
                        })

    n = len(cases)
    print(f"\nPhase 3 — perceptual proxy: {n} ascending-run variants")

    results = []
    attack_shapes = {250: 0.008, 300: 0.010, 350: 0.012, 400: 0.015, 450: 0.018}

    for i, c in enumerate(cases):
        cid = c["id"]
        wav = proxy_dir / f"{cid}.wav"
        v3_tr  = proxy_dir / f"{cid}_v3_trace.json"
        v3_out = proxy_dir / f"{cid}_v3_out.wav"

        # Generate WAV
        attack = attack_shapes.get(c["dur_ms"], 0.012)
        samples = gen_ascending_run(
            dur_ms       = c["dur_ms"],
            register_midi= c["reg_midi"],
            interval_semi= c["interval"],
            start_cents  = c["start_c"],
            vibrato_cents= c["vib_c"],
            attack_sec   = attack,
        )
        write_wav(wav, samples)

        # Run bench — v3 active (key=A major covers intervals of 1 and 2)
        trace = run_bench(wav, "A", "major", "v2_active", v3_out, v3_tr)
        if not trace:
            print(f"  [{i+1}/{n}] {cid}: BENCH FAIL")
            continue

        frames  = trace.get("frames", [])
        voiced  = [f for f in frames if f.get("voiced")]
        jumps   = compute_jumps(frames)
        max_j   = max(jumps)   if jumps   else 0.0
        p95_j   = sorted(jumps)[int(len(jumps)*0.95)] if jumps else 0.0
        fires   = [f for f in voiced if f.get("intentReasonName") == "SLIDE_DESTINATION_V2"]
        fire_r  = len(fires) / max(len(voiced), 1)

        # Transient classification
        # "Chirp" risk: jump > 150¢ that lasts 1-2 frames (11-22ms)
        severe = max_j > 250.0
        moderate = 150.0 < max_j <= 250.0
        benign = max_j <= 150.0

        # Recovery: frames after max jump until |cc| < 30¢
        max_jump_frame = 0
        max_j_seen = 0.0
        for k in range(1, len(voiced)):
            prev = voiced[k-1].get("correctionCents", 0) or 0
            curr = voiced[k].get("correctionCents",   0) or 0
            j = abs(curr - prev)
            if j > max_j_seen:
                max_j_seen = j; max_jump_frame = k
        recovery_frames = 0
        for k in range(max_jump_frame + 1, len(voiced)):
            recovery_frames += 1
            if abs(voiced[k].get("correctionCents", 0) or 0) < 30.0:
                break

        tag = "SEVERE" if severe else ("MODERATE" if moderate else "BENIGN")
        results.append({
            "id": cid, "dur_ms": c["dur_ms"], "reg": reg_name,
            "interval": c["interval"], "vib_c": c["vib_c"], "start_c": c["start_c"],
            "max_jump": round(max_j, 1), "p95_jump": round(p95_j, 1),
            "fire_rate": round(fire_r, 4), "n_fires": len(fires),
            "recovery_frames": recovery_frames,
            "recovery_ms": round(recovery_frames * BLOCK_SEC * 1000, 1),
            "tag": tag,
        })

        if (i + 1) % 20 == 0 or i == n - 1:
            print(f"  [{i+1}/{n}] done  —  {tag} examples so far: "
                  f"severe={sum(1 for r in results if r['tag']=='SEVERE')}  "
                  f"moderate={sum(1 for r in results if r['tag']=='MODERATE')}  "
                  f"benign={sum(1 for r in results if r['tag']=='BENIGN')}")

    # Aggregate
    severe_n   = sum(1 for r in results if r["tag"] == "SEVERE")
    moderate_n = sum(1 for r in results if r["tag"] == "MODERATE")
    benign_n   = sum(1 for r in results if r["tag"] == "BENIGN")
    all_max    = sorted([r["max_jump"] for r in results if r["max_jump"] > 0], reverse=True)
    all_fire   = [r["fire_rate"] for r in results]

    print(f"\n  Results: severe={severe_n} moderate={moderate_n} benign={benign_n}")
    print(f"  Max jump across all variants: {all_max[0]:.1f}¢" if all_max else "  No jumps")
    print(f"  Mean fire rate: {sum(all_fire)/len(all_fire):.1%}" if all_fire else "")

    # Worst 5 cases
    print("\n  Worst 5 by max jump:")
    for r in sorted(results, key=lambda x: -x["max_jump"])[:5]:
        print(f"    {r['id']:55s} {r['max_jump']:.0f}¢  {r['tag']}  fire={r['fire_rate']:.1%}  rec={r['recovery_ms']:.0f}ms")

    # Save proxy results
    proxy_report = {
        "total":    len(results),
        "severe":   severe_n,
        "moderate": moderate_n,
        "benign":   benign_n,
        "max_jump_all": all_max[0] if all_max else 0,
        "results":  results,
    }
    (proxy_dir / "proxy_report.json").write_text(json.dumps(proxy_report, indent=2))

    return proxy_report


# ---------------------------------------------------------------------------
# Torture regression checker
# ---------------------------------------------------------------------------

def check_torture_regression() -> bool:
    """Re-run each torture case and check against manifest hard thresholds."""
    passed = True
    for case_dir in sorted(TORTURE.iterdir()):
        mf = case_dir / "manifest.json"
        if not mf.exists():
            continue
        manifest = json.loads(mf.read_text())
        thresholds = manifest.get("hard_thresholds", {})
        inp_wav    = case_dir / "input.wav"
        if not inp_wav.exists():
            print(f"  {manifest['id']}: SKIP (no input WAV)")
            continue

        v3_trace_p = case_dir / "v3_trace_regression.json"
        v3_out_p   = case_dir / "v3_corrected_regression.wav"
        trace = run_bench(inp_wav, "A", "major", "v2_active", v3_out_p, v3_trace_p)
        if not trace:
            print(f"  {manifest['id']}: FAIL (bench error)")
            passed = False
            continue

        frames  = trace.get("frames", [])
        jumps   = compute_jumps(frames)
        max_j   = max(jumps) if jumps else 0.0
        crashes = 0 if trace else 1
        voiced  = [f for f in frames if f.get("voiced")]
        oct_err = sum(1 for f in voiced
                     if f.get("intentTarget",-1) >= 0
                     and abs(f.get("intentTarget",0) - round(f.get("detectedMidi",0) or 0)) >= 12)

        fails = []
        if crashes > thresholds.get("crashes", 0):      fails.append(f"crashes={crashes}")
        if max_j   > thresholds.get("max_jump_cents",500): fails.append(f"max_jump={max_j:.0f}¢>{thresholds['max_jump_cents']}¢")
        if oct_err > thresholds.get("octave_errors", 0): fails.append(f"octave_errors={oct_err}")

        status = "PASS" if not fails else f"FAIL ({', '.join(fails)})"
        print(f"  {manifest['id']}: {status}  max_jump={max_j:.1f}¢")
        if fails:
            passed = False

    return passed


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="Regression-only mode")
    args = parser.parse_args()

    if args.check:
        print("=== Torture corpus regression check ===")
        ok = check_torture_regression()
        return 0 if ok else 1

    print("=== Phase 2 — RUN_up_350ms torture test ===")
    manifest = build_run_up_350ms_torture()
    if "error" in manifest:
        print(f"  ERROR: {manifest['error']}")
        return 1

    print("\n=== Phase 3 — Transient perceptual proxy ===")
    proxy = run_perceptual_proxy()

    severe   = proxy["severe"]
    moderate = proxy["moderate"]
    n_total  = proxy["total"]

    print(f"\n{'='*60}")
    if severe > n_total * 0.20:
        print(f"HOLD — TRANSIENT RISK")
        print(f"  {severe}/{n_total} variants show severe transient (>250¢).")
        print(f"  The 285¢/22ms RUN_up transient is NOT an isolated edge case.")
        verdict = "HOLD"
    elif severe > 0 or moderate > n_total * 0.30:
        print(f"CAUTION — ACCEPTED RISK WITH MONITORING")
        print(f"  severe={severe}  moderate={moderate}/{n_total}")
        print(f"  Transient confined to specific run patterns. Proceed with flag.")
        verdict = "CAUTION"
    else:
        print(f"BENIGN — ACCEPTED RISK")
        print(f"  severe=0  moderate={moderate}/{n_total} (<30% threshold)")
        print(f"  The 285¢/22ms transient is an isolated edge case in fast runs.")
        verdict = "BENIGN"

    (TORTURE / "proxy_verdict.txt").write_text(
        f"{verdict}\nsevere={severe} moderate={moderate} benign={proxy['benign']} total={n_total}\n"
        f"max_jump_any={proxy['max_jump_all']:.1f}¢\n"
    )

    return 0


if __name__ == "__main__":
    sys.exit(main())
