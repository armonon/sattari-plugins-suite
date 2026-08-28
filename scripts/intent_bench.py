#!/usr/bin/env python3
"""
IntentBench — Phase 1-6 Intent Engine Baseline Study
Production baseline: 6c022cb (transitionBoost hysteresis)

Phase 1: Build IntentBench corpus + record baseline target behavior
Phase 2: Classify intent failures per frame
Phase 3: Prototype shadow Intent Engine
Phase 4: Shadow comparison (production vs intent engine)
Phase 5: Adversarial search around failure clusters
Phase 6: Candidate gate evaluation + final report

Strategy G correction dynamics are FROZEN. This only touches the
note-selection (targeting) layer, not the correction curve.
"""

import json
import math
import os
import struct
import subprocess
import sys
import wave
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

REPO   = Path(__file__).resolve().parent.parent
BENCH  = REPO / "build" / "plugins" / "auto-pitch" / "AutoPitchBench"
OUT    = REPO / "vocalbench-results" / "intent-bench"
SR     = 44100
BLOCK  = 512

OUT.mkdir(parents=True, exist_ok=True)
(OUT / "corpus").mkdir(exist_ok=True)
(OUT / "traces").mkdir(exist_ok=True)
(OUT / "adversarial").mkdir(exist_ok=True)

# ---------------------------------------------------------------------------
# Synthetic audio helpers
# ---------------------------------------------------------------------------

def midi_to_hz(m: float) -> float:
    return 440.0 * (2.0 ** ((m - 69.0) / 12.0))

def write_wav(path: Path, samples: list[float], sr: int = SR):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'w') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        data = struct.pack(f'<{len(samples)}h',
                           *[max(-32767, min(32767, int(s * 32767))) for s in samples])
        wf.writeframes(data)

def sine_segment(hz: float, dur_s: float, amp: float = 0.7, sr: int = SR,
                 phase0: float = 0.0) -> tuple[list[float], float]:
    n = int(dur_s * sr)
    samples = []
    phase = phase0
    inc = 2.0 * math.pi * hz / sr
    for _ in range(n):
        samples.append(amp * math.sin(phase))
        phase += inc
    return samples, phase % (2.0 * math.pi)

def silence(dur_s: float, sr: int = SR) -> list[float]:
    return [0.0] * int(dur_s * sr)

def vibrato_segment(center_hz: float, depth_cents: float, rate_hz: float,
                    dur_s: float, amp: float = 0.7, sr: int = SR,
                    phase0: float = 0.0) -> tuple[list[float], float]:
    n = int(dur_s * sr)
    samples = []
    phase = phase0
    lfo_phase = 0.0
    lfo_inc = 2.0 * math.pi * rate_hz / sr
    for _ in range(n):
        lfo = math.sin(lfo_phase)
        freq = center_hz * (2.0 ** (lfo * depth_cents / 1200.0))
        inc = 2.0 * math.pi * freq / sr
        samples.append(amp * math.sin(phase))
        phase += inc
        lfo_phase += lfo_inc
    return samples, phase % (2.0 * math.pi)

def slide_segment(start_hz: float, end_hz: float, dur_s: float,
                  amp: float = 0.7, sr: int = SR,
                  phase0: float = 0.0) -> tuple[list[float], float]:
    n = int(dur_s * sr)
    samples = []
    phase = phase0
    for i in range(n):
        t = i / n
        hz = start_hz * (end_hz / start_hz) ** t
        inc = 2.0 * math.pi * hz / sr
        samples.append(amp * math.sin(phase))
        phase += inc
    return samples, phase % (2.0 * math.pi)

def apply_envelope(samples: list[float], attack_s: float = 0.01,
                   release_s: float = 0.02, sr: int = SR) -> list[float]:
    n = len(samples)
    na = min(int(attack_s * sr), n)
    nr = min(int(release_s * sr), n)
    out = list(samples)
    for i in range(na):
        out[i] *= i / na
    for i in range(nr):
        out[n - 1 - i] *= i / nr
    return out

# ---------------------------------------------------------------------------
# Corpus case definitions
# ---------------------------------------------------------------------------
# Each case: name, ground_truth_target (MIDI note the singer INTENDS),
#             key, scale, WAV generator lambda

A3  = 57  # A3
B3  = 59  # B3
C4  = 60
D4  = 62
E4  = 64
F4  = 65
G4  = 67
A4  = 69

def make_corpus():
    cases = []

    # --- Boundary cases ±40-60¢ ---
    for offset_c, name in [(-40, "m40"), (-50, "m50"), (-60, "m60"),
                            (+40, "p40"), (+50, "p50"), (+60, "p60")]:
        midi = A3
        hz = midi_to_hz(midi + offset_c / 100.0)
        def gen(hz=hz):
            s, _ = sine_segment(hz, 2.5)
            return apply_envelope(s)
        cases.append({
            "id": f"BC_A3_{name}",
            "category": "boundary",
            "intended": midi,
            "key": "A", "scale": "minor",
            "gen": gen,
        })

    # --- Semitone slide (A3→B3) ---
    for dur_ms in [100, 200, 400]:
        def gen(dur=dur_ms/1000):
            pre, p = sine_segment(midi_to_hz(A3), 0.5)
            sl, p = slide_segment(midi_to_hz(A3), midi_to_hz(B3), dur, phase0=p)
            post, _ = sine_segment(midi_to_hz(B3), 1.0, phase0=p)
            return apply_envelope(pre + sl + post)
        cases.append({
            "id": f"SLIDE_semi_{dur_ms}ms",
            "category": "semitone_slide",
            "intended": B3,
            "key": "A", "scale": "minor",
            "gen": gen,
        })

    # --- Whole-tone slide (A3→B3 via semitone then whole) ---
    for dur_ms in [200, 500]:
        def gen(dur=dur_ms/1000):
            pre, p = sine_segment(midi_to_hz(A3), 0.4)
            sl, p = slide_segment(midi_to_hz(A3), midi_to_hz(B3 + 2), dur, phase0=p)
            post, _ = sine_segment(midi_to_hz(B3 + 2), 1.0, phase0=p)
            return apply_envelope(pre + sl + post)
        cases.append({
            "id": f"SLIDE_whole_{dur_ms}ms",
            "category": "whole_tone_slide",
            "intended": B3 + 2,
            "key": "A", "scale": "minor",
            "gen": gen,
        })

    # --- Scoop into note (approach from below) ---
    for depth_c in [30, 60, 100]:
        def gen(d=depth_c):
            start_hz = midi_to_hz(A3 - d / 100.0)
            sl, p = slide_segment(start_hz, midi_to_hz(A3), 0.15)
            post, _ = sine_segment(midi_to_hz(A3), 1.5, phase0=p)
            return apply_envelope(sl + post, attack_s=0.001)
        cases.append({
            "id": f"SCOOP_A3_{depth_c}c",
            "category": "scoop",
            "intended": A3,
            "key": "A", "scale": "minor",
            "gen": gen,
        })

    # --- Fall/release (note into downward slide) ---
    for depth_c in [50, 100]:
        def gen(d=depth_c):
            pre, p = sine_segment(midi_to_hz(A3), 1.0)
            sl, _ = slide_segment(midi_to_hz(A3), midi_to_hz(A3 - d / 100.0),
                                   0.3, amp=0.5, phase0=p)
            return apply_envelope(pre + sl, release_s=0.05)
        cases.append({
            "id": f"FALL_A3_{depth_c}c",
            "category": "fall",
            "intended": A3,
            "key": "A", "scale": "minor",
            "gen": gen,
        })

    # --- Rapid runs (scale up) ---
    run_notes = [A3, B3, C4, D4, E4]
    for note_dur_ms in [80, 120, 200]:
        def gen(nd=note_dur_ms/1000):
            out = []
            p = 0.0
            for note in run_notes:
                seg, p = sine_segment(midi_to_hz(note), nd, phase0=p)
                out += apply_envelope(seg, attack_s=0.005, release_s=0.005)
            return out
        cases.append({
            "id": f"RUN_up_{note_dur_ms}ms",
            "category": "run",
            "intended": E4,
            "key": "A", "scale": "minor",
            "gen": gen,
        })

    # --- Repeated notes ---
    for n_reps in [4, 8]:
        def gen(nr=n_reps):
            out = []
            p = 0.0
            for _ in range(nr):
                seg, p = sine_segment(midi_to_hz(A3), 0.15, phase0=p)
                out += apply_envelope(seg, attack_s=0.008, release_s=0.008)
                out += silence(0.04)
            return out
        cases.append({
            "id": f"REPEAT_A3_{n_reps}x",
            "category": "repeated_note",
            "intended": A3,
            "key": "A", "scale": "minor",
            "gen": gen,
        })

    # --- Vibrato crossing note boundary (T10 equivalent) ---
    for depth_c in [30, 50, 70]:
        center = A3 + 0.5  # halfway between A3 and B3 in MIDI
        def gen(d=depth_c, c=center):
            seg, _ = vibrato_segment(midi_to_hz(c), d, 5.0, 3.0)
            return apply_envelope(seg)
        cases.append({
            "id": f"VIB_cross_{depth_c}c",
            "category": "vibrato_boundary",
            "intended": A3,
            "key": "A", "scale": "minor",
            "gen": gen,
        })

    # --- Short notes 100-500ms ---
    for dur_ms in [100, 200, 350, 500]:
        def gen(d=dur_ms/1000):
            seg, _ = sine_segment(midi_to_hz(A3), d)
            return apply_envelope(seg, attack_s=0.01, release_s=0.02)
        cases.append({
            "id": f"SHORT_A3_{dur_ms}ms",
            "category": "short_note",
            "intended": A3,
            "key": "A", "scale": "minor",
            "gen": gen,
        })

    # --- Chromatic passing tones ---
    # Singer passes through Bb3 (not in A minor) on way to B3
    def gen_chromatic_pass():
        a, p = sine_segment(midi_to_hz(A3), 0.4)
        bb, p = sine_segment(midi_to_hz(A3 + 1), 0.10, phase0=p)  # Bb3 passing
        b, _ = sine_segment(midi_to_hz(B3), 0.8, phase0=p)
        return apply_envelope(a + bb + b, attack_s=0.005)
    cases.append({
        "id": "CHROM_pass_A3_Bb3_B3",
        "category": "chromatic_passing",
        "intended": B3,
        "key": "A", "scale": "minor",
        "gen": gen_chromatic_pass,
    })

    # Longer chromatic run
    chrom_notes = [A3, A3+1, B3, C4, C4+1, D4]
    def gen_chrom_run():
        out = []
        p = 0.0
        for note in chrom_notes:
            seg, p = sine_segment(midi_to_hz(note), 0.12, phase0=p)
            out += apply_envelope(seg, attack_s=0.005, release_s=0.005)
        return out
    cases.append({
        "id": "CHROM_run_A3_D4",
        "category": "chromatic_run",
        "intended": D4,
        "key": "A", "scale": "minor",
        "gen": gen_chrom_run,
    })

    return cases


# ---------------------------------------------------------------------------
# Run bench and parse trace
# ---------------------------------------------------------------------------

def run_bench(wav_in: Path, key: str, scale: str) -> Optional[list[dict]]:
    trace_path = OUT / "traces" / (wav_in.stem + "_trace.json")
    wav_out    = OUT / "traces" / (wav_in.stem + "_out.wav")
    cmd = [
        str(BENCH),
        str(wav_in),
        "--output", str(wav_out),
        "--trace",  str(trace_path),
        "--key",    key,
        "--scale",  scale,
        "--amount", "0.82",
        "--speed",  "0.70",
        "--block",  str(BLOCK),
    ]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(f"  BENCH ERROR: {r.stderr[:200]}")
        return None
    if not trace_path.exists():
        print(f"  No trace produced.")
        return None
    with open(trace_path) as f:
        data = json.load(f)
    return data.get("frames", [])


# ---------------------------------------------------------------------------
# Phase 1: enrich frames with derived metrics
# ---------------------------------------------------------------------------

def enrich_frames(frames: list[dict], intended: int) -> list[dict]:
    prev_target = None
    stability_count = 0
    target_history = []

    for i, fr in enumerate(frames):
        tgt = fr.get("targetMidi") or fr.get("detectedMidi", -1)
        det = fr.get("detectedMidi", -1)
        conf = fr.get("confidence", 0.0)

        # Pitch velocity: frame-to-frame MIDI delta
        if i > 0 and frames[i-1].get("voiced") and fr.get("voiced"):
            pv = abs(det - frames[i-1].get("detectedMidi", det)) * (SR / BLOCK)
        else:
            pv = 0.0
        fr["pitchVelocity_midiPerSec"] = round(pv, 4)

        # Target stability
        if tgt != prev_target:
            stability_count = 0
            prev_target = tgt
        else:
            stability_count += 1
        fr["targetMidi"] = tgt
        fr["targetStabilityFrames"] = stability_count
        fr["targetStabilitySec"] = round(stability_count * BLOCK / SR, 4)

        # Recent target history (last 5 unique targets)
        if not target_history or target_history[-1] != tgt:
            target_history.append(tgt)
            if len(target_history) > 5:
                target_history.pop(0)
        fr["recentTargets"] = list(target_history)

        # Intended note delta
        fr["intendedMidi"] = intended
        fr["targetError_semitones"] = round(abs(tgt - intended), 2) if tgt != -1 else None

    return frames


# ---------------------------------------------------------------------------
# Phase 2: intent failure classification
# ---------------------------------------------------------------------------

FAILURE_TYPES = {
    "CORRECT_PRESERVE":    "Target matches intended; pitch expressive",
    "CORRECT_STABLE":      "Target matches intended; pitch stable",
    "FALSE_TARGET":        "Target is wrong note (not intended, not slide path)",
    "PREMATURE_TARGET":    "Target is correct but locked too early in slide",
    "CHATTER":             "Target oscillates between notes unnecessarily",
    "LATE_ACQUISITION":    "Correct target selected too slowly",
    "UNVOICED":            "Frame is unvoiced",
    "AMBIGUOUS":           "Boundary case; confidence too low to classify",
}

def classify_frame(fr: dict, all_frames: list[dict], idx: int,
                   intended: int, category: str) -> str:
    if not fr.get("voiced"):
        return "UNVOICED"

    tgt  = fr.get("targetMidi", -1)
    conf = fr.get("confidence", 0.0)
    pv   = fr.get("pitchVelocity_midiPerSec", 0.0)
    stab = fr.get("targetStabilitySec", 0.0)
    det  = fr.get("detectedMidi", -1)
    hist = fr.get("recentTargets", [])

    if conf < 0.40:
        return "AMBIGUOUS"

    on_intended = (tgt == intended)
    dist_to_intended = abs(tgt - intended) if tgt != -1 else 99

    # Chatter: target alternates rapidly between 2+ values
    if len(set(hist)) >= 2 and stab < 0.05 and not on_intended:
        return "CHATTER"

    # For slide/scoop/run categories, premature is target locked before
    # pitch arrives at destination
    if category in ("semitone_slide", "whole_tone_slide", "scoop", "run"):
        if on_intended and pv > 0.5 and stab < 0.12:
            return "PREMATURE_TARGET"

    # False target: target is clearly wrong note (>1 semitone from intended,
    # not during a slide)
    if dist_to_intended > 1 and category not in ("run", "chromatic_run",
                                                   "chromatic_passing"):
        return "FALSE_TARGET"

    # Late acquisition: we're past the expected acquisition window and still wrong
    t = fr.get("timeSec", 0.0)
    total_t = all_frames[-1].get("timeSec", 1.0) if all_frames else 1.0
    if not on_intended and t > total_t * 0.5:
        return "LATE_ACQUISITION"

    if on_intended and pv < 0.3:
        return "CORRECT_STABLE"

    if on_intended:
        return "CORRECT_PRESERVE"

    return "AMBIGUOUS"


def classify_case(frames: list[dict], intended: int, category: str) -> list[dict]:
    voiced = [f for f in frames if f.get("voiced")]
    for i, fr in enumerate(frames):
        fr["intentClass"] = classify_frame(fr, voiced, i, intended, category)
    return frames


# ---------------------------------------------------------------------------
# Phase 3: Shadow Intent Engine
# ---------------------------------------------------------------------------

@dataclass
class IntentState:
    prev_stable_midi: int = -1
    stable_frames: int = 0
    pitch_direction: float = 0.0  # smoothed
    last_voiced_midi: float = -1.0
    candidate: int = -1
    confidence: float = 0.0
    reason: str = ""


def shadow_intent_engine(frames: list[dict], key: str, scale: str,
                          intended: int) -> list[dict]:
    """
    Experimental parallel estimator — never alters production audio.
    Outputs: shadowCandidate, shadowConfidence, shadowReason per frame.
    """
    state = IntentState()
    KEY_INTERVALS = {
        "major":    [0, 2, 4, 5, 7, 9, 11],
        "minor":    [0, 2, 3, 5, 7, 8, 10],
        "chromatic": list(range(12)),
    }
    root_map = {"C":0,"C#":1,"Db":1,"D":2,"D#":3,"Eb":3,"E":4,"F":5,
                "F#":6,"Gb":6,"G":7,"G#":8,"Ab":8,"A":9,"A#":10,"Bb":10,"B":11}
    root_idx = root_map.get(key, 9)
    intervals = KEY_INTERVALS.get(scale, KEY_INTERVALS["minor"])

    def nearest_scale_midi(midi_float: float) -> int:
        best, best_d = int(round(midi_float)), 999.0
        for c in range(int(midi_float) - 13, int(midi_float) + 14):
            if ((c - root_idx) % 12) in intervals:
                d = abs(c - midi_float)
                if d < best_d:
                    best_d, best = d, c
        return best

    for i, fr in enumerate(frames):
        if not fr.get("voiced"):
            fr["shadowCandidate"] = -1
            fr["shadowConfidence"] = 0.0
            fr["shadowReason"] = "unvoiced"
            continue

        det  = fr.get("detectedMidi", -1)
        pv   = fr.get("pitchVelocity_midiPerSec", 0.0)
        conf = fr.get("confidence", 0.0)

        # Smooth pitch direction
        alpha = 0.3
        if i > 0 and frames[i-1].get("voiced"):
            delta = det - frames[i-1].get("detectedMidi", det)
        else:
            delta = 0.0
        state.pitch_direction = (1.0 - alpha) * state.pitch_direction + alpha * delta

        # Stability tracking
        if state.last_voiced_midi != -1:
            drift = abs(det - state.last_voiced_midi)
            if drift < 0.3:
                state.stable_frames += 1
            else:
                state.stable_frames = max(0, state.stable_frames - 2)
        state.last_voiced_midi = det

        # --- Intent candidate logic ---
        stability_sec = state.stable_frames * BLOCK / SR
        is_sliding = pv > 0.8  # moving faster than 0.8 MIDI/sec

        if is_sliding and abs(state.pitch_direction) > 0.3:
            # Extrapolate destination: project current pitch + direction 150ms
            lookahead = 0.15 * state.pitch_direction * (SR / BLOCK)
            projected = det + lookahead
            candidate = nearest_scale_midi(projected)
            i_conf = min(0.85, conf * 0.9)
            reason = f"slide→{candidate} (dir={state.pitch_direction:+.2f})"
        elif stability_sec > 0.08 and conf > 0.55:
            # Stable note: high confidence in current
            candidate = nearest_scale_midi(det)
            i_conf = min(0.95, conf)
            state.prev_stable_midi = candidate
            reason = f"stable ({stability_sec*1000:.0f}ms)"
        elif state.prev_stable_midi != -1 and pv < 0.5 and conf > 0.40:
            # Drifting from a previously stable note — preserve it
            candidate = state.prev_stable_midi
            i_conf = 0.55
            reason = f"preserve {state.prev_stable_midi} (drift)"
        else:
            # Low confidence: nearest scale note
            candidate = nearest_scale_midi(det)
            i_conf = conf * 0.6
            reason = f"nearest ({conf:.2f} conf)"

        fr["shadowCandidate"] = candidate
        fr["shadowConfidence"] = round(i_conf, 3)
        fr["shadowReason"] = reason

    return frames


# ---------------------------------------------------------------------------
# Phase 4: Shadow comparison metrics
# ---------------------------------------------------------------------------

def compare_shadow(frames: list[dict], intended: int) -> dict:
    voiced = [f for f in frames if f.get("voiced")]
    if not voiced:
        return {}

    prod_correct = sum(1 for f in voiced if f.get("targetMidi") == intended)
    shadow_correct = sum(1 for f in voiced if f.get("shadowCandidate") == intended)
    false_prod   = sum(1 for f in voiced
                       if f.get("intentClass") == "FALSE_TARGET")
    false_shadow = sum(1 for f in voiced
                       if f.get("shadowCandidate") != intended
                       and f.get("shadowCandidate") != -1
                       and abs(f.get("shadowCandidate", 99) - intended) > 1)
    chatter_prod = sum(1 for f in voiced if f.get("intentClass") == "CHATTER")

    n = len(voiced)
    return {
        "voiced_frames":        n,
        "prod_accuracy":        round(prod_correct / n, 4),
        "shadow_accuracy":      round(shadow_correct / n, 4),
        "shadow_improvement":   round((shadow_correct - prod_correct) / n, 4),
        "prod_false_target_rate":   round(false_prod / n, 4),
        "shadow_false_target_rate": round(false_shadow / n, 4),
        "prod_chatter_rate":    round(chatter_prod / n, 4),
    }


# ---------------------------------------------------------------------------
# Phase 5: Adversarial search
# ---------------------------------------------------------------------------

def adversarial_search(base_case: dict, n: int = 12) -> list[dict]:
    adv_cases = []
    cat = base_case["category"]
    intended = base_case["intended"]

    if cat == "boundary":
        for off in [-55, -52, -48, -45, +45, +48, +52, +55]:
            hz = midi_to_hz(intended + off / 100.0)
            def gen(h=hz):
                s, _ = sine_segment(h, 2.0)
                return apply_envelope(s)
            adv_cases.append({
                "id": f"ADV_{base_case['id']}_off{off:+d}c",
                "category": "adv_boundary",
                "intended": intended,
                "key": base_case["key"], "scale": base_case["scale"],
                "gen": gen,
            })

    elif cat == "vibrato_boundary":
        for depth_c in [20, 25, 35, 45, 60, 80]:
            center = intended + 0.5
            def gen(d=depth_c, c=center):
                seg, _ = vibrato_segment(midi_to_hz(c), d, 5.0, 3.0)
                return apply_envelope(seg)
            adv_cases.append({
                "id": f"ADV_{base_case['id']}_dep{depth_c}c",
                "category": "adv_vib_boundary",
                "intended": intended,
                "key": base_case["key"], "scale": base_case["scale"],
                "gen": gen,
            })

    elif cat in ("semitone_slide", "scoop"):
        for dur_ms in [75, 100, 150, 300, 500, 750]:
            def gen(d=dur_ms/1000):
                pre, p = sine_segment(midi_to_hz(intended - 2), 0.4)
                sl, p = slide_segment(midi_to_hz(intended - 2), midi_to_hz(intended),
                                       d, phase0=p)
                post, _ = sine_segment(midi_to_hz(intended), 1.0, phase0=p)
                return apply_envelope(pre + sl + post)
            adv_cases.append({
                "id": f"ADV_{base_case['id']}_dur{dur_ms}ms",
                "category": "adv_slide",
                "intended": intended,
                "key": base_case["key"], "scale": base_case["scale"],
                "gen": gen,
            })

    return adv_cases[:n]


# ---------------------------------------------------------------------------
# Phase 6: Gate check
# ---------------------------------------------------------------------------

def check_gate(all_results: list[dict]) -> dict:
    all_voiced = []
    for r in all_results:
        all_voiced.extend([f for f in r.get("frames", []) if f.get("voiced")])

    if not all_voiced:
        return {"pass": False, "reason": "no voiced frames"}

    n = len(all_voiced)
    false_rate   = sum(1 for f in all_voiced if f.get("intentClass") == "FALSE_TARGET") / n
    chatter_rate = sum(1 for f in all_voiced if f.get("intentClass") == "CHATTER") / n
    shadow_acc   = sum(1 for f in all_voiced if f.get("shadowCandidate") == f.get("intendedMidi")) / n
    prod_acc     = sum(1 for f in all_voiced if f.get("targetMidi") == f.get("intendedMidi")) / n
    shadow_improvement = shadow_acc - prod_acc
    octave_errors = sum(1 for f in all_voiced
                        if abs((f.get("targetMidi", 0) or 0) - (f.get("intendedMidi", 0) or 0)) >= 12)

    gate_pass = (
        octave_errors == 0
        and shadow_improvement >= 0.0  # shadow no worse than production
        and false_rate < 0.30          # fewer than 30% false targets
    )

    return {
        "pass": gate_pass,
        "voiced_frames": n,
        "prod_accuracy": round(prod_acc, 4),
        "shadow_accuracy": round(shadow_acc, 4),
        "shadow_improvement": round(shadow_improvement, 4),
        "false_target_rate": round(false_rate, 4),
        "chatter_rate": round(chatter_rate, 4),
        "octave_errors": octave_errors,
    }


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    if not BENCH.exists():
        print(f"ERROR: bench binary not found at {BENCH}")
        sys.exit(1)

    print("IntentBench — Phase 1-6 Baseline Study")
    print("=" * 50)
    print(f"Production baseline: 6c022cb")
    print(f"Output: {OUT}\n")

    # Phase 1: Build corpus
    print("Phase 1: Building corpus...")
    cases = make_corpus()
    print(f"  {len(cases)} base cases\n")

    all_results = []

    for case in cases:
        wav_path = OUT / "corpus" / f"{case['id']}.wav"
        samples = case["gen"]()
        write_wav(wav_path, samples)

    # Phase 5: Adversarial cases
    print("Phase 5: Generating adversarial cases...")
    adv_seed_cats = ["boundary", "vibrato_boundary", "semitone_slide"]
    adv_cases = []
    for case in cases:
        if case["category"] in adv_seed_cats:
            adv = adversarial_search(case, n=6)
            for a in adv:
                wav_path = OUT / "adversarial" / f"{a['id']}.wav"
                samples = a["gen"]()
                write_wav(wav_path, samples)
            adv_cases.extend(adv)
    print(f"  {len(adv_cases)} adversarial cases\n")

    all_cases = cases + adv_cases
    print(f"Phase 1-4: Running bench + analysis ({len(all_cases)} cases)...")

    failure_counts = {k: 0 for k in FAILURE_TYPES}
    shadow_improvements = []

    for i, case in enumerate(all_cases):
        wav_dir = "adversarial" if case["category"].startswith("adv_") else "corpus"
        wav_path = OUT / wav_dir / f"{case['id']}.wav"
        print(f"  [{i+1}/{len(all_cases)}] {case['id']} ... ", end="", flush=True)

        frames = run_bench(wav_path, case["key"], case["scale"])
        if frames is None:
            print("SKIP")
            continue

        frames = enrich_frames(frames, case["intended"])
        frames = classify_case(frames, case["intended"], case["category"])
        frames = shadow_intent_engine(frames, case["key"], case["scale"], case["intended"])
        metrics = compare_shadow(frames, case["intended"])

        for f in frames:
            ic = f.get("intentClass", "AMBIGUOUS")
            if ic in failure_counts:
                failure_counts[ic] += 1

        si = metrics.get("shadow_improvement", 0.0)
        shadow_improvements.append(si)
        verdict = "+" if si >= 0 else "-"
        fa = metrics.get("prod_accuracy", 0)
        sa = metrics.get("shadow_accuracy", 0)
        print(f"prod={fa:.0%} shadow={sa:.0%} {verdict}")

        result = {
            "id": case["id"],
            "category": case["category"],
            "intended": case["intended"],
            "frames": frames,
            "metrics": metrics,
        }
        all_results.append(result)

        # Save per-case JSON
        out_json = OUT / "traces" / f"{case['id']}_intent.json"
        with open(out_json, "w") as f_out:
            json.dump(result, f_out, indent=2)

    # Phase 6: Gate
    print("\nPhase 6: Gate evaluation...")
    gate = check_gate(all_results)

    # Write baseline report
    _write_report(all_results, failure_counts, gate, shadow_improvements)

    verdict = "GO" if gate["pass"] else "HOLD"
    print(f"\n{'='*50}")
    print(f"INTENT ENGINE BASELINE REPORT: {verdict}")
    print(f"Voiced frames analyzed: {gate.get('voiced_frames', 0)}")
    print(f"Production accuracy:    {gate.get('prod_accuracy', 0):.1%}")
    print(f"Shadow accuracy:        {gate.get('shadow_accuracy', 0):.1%}")
    print(f"Shadow improvement:     {gate.get('shadow_improvement', 0):+.1%}")
    print(f"False-target rate:      {gate.get('false_target_rate', 0):.1%}")
    print(f"Chatter rate:           {gate.get('chatter_rate', 0):.1%}")
    print(f"Octave errors:          {gate.get('octave_errors', 0)}")
    print(f"\nReport: {OUT}/INTENT_ENGINE_BASELINE_REPORT.md")


def _write_report(results, failure_counts, gate, shadow_improvements):
    total_voiced = sum(r["metrics"].get("voiced_frames", 0) for r in results)
    n_cases = len(results)

    # Category breakdown
    cats = {}
    for r in results:
        c = r["category"]
        cats.setdefault(c, {"n": 0, "prod_acc": [], "shadow_acc": [], "false": []})
        cats[c]["n"] += 1
        m = r["metrics"]
        cats[c]["prod_acc"].append(m.get("prod_accuracy", 0))
        cats[c]["shadow_acc"].append(m.get("shadow_accuracy", 0))
        cats[c]["false"].append(m.get("prod_false_target_rate", 0))

    def mean(lst): return sum(lst) / len(lst) if lst else 0.0

    lines = [
        "# Intent Engine Baseline Report",
        "",
        f"**Production baseline:** `6c022cb`",
        f"**Cases:** {n_cases} total ({sum(1 for r in results if not r['category'].startswith('adv_'))} base + {sum(1 for r in results if r['category'].startswith('adv_'))} adversarial)",
        f"**Voiced frames analyzed:** {total_voiced:,}",
        "",
        "---",
        "",
        "## Phase 6 — Candidate Gate",
        "",
        "| Check | Result | Status |",
        "|-------|--------|--------|",
        f"| Shadow accuracy ≥ production | {gate.get('shadow_accuracy', 0):.1%} vs {gate.get('prod_accuracy', 0):.1%} | {'✅' if gate.get('shadow_improvement', -1) >= 0 else '❌'} |",
        f"| False-target rate | {gate.get('false_target_rate', 0):.1%} | {'✅' if gate.get('false_target_rate', 1) < 0.30 else '❌'} |",
        f"| Octave errors | {gate.get('octave_errors', 0)} | {'✅' if gate.get('octave_errors', 1) == 0 else '❌'} |",
        f"| Chatter rate | {gate.get('chatter_rate', 0):.1%} | ℹ️ |",
        "",
        f"**RECOMMENDATION: {'GO — proceed to active-audio testing' if gate.get('pass') else 'HOLD — shadow engine needs refinement before audio activation'}**",
        "",
        "---",
        "",
        "## Phase 2 — Intent Failure Distribution",
        "",
        "| Class | Frames | % |",
        "|-------|--------|---|",
    ]
    total_frames = sum(failure_counts.values()) or 1
    for cls, desc in FAILURE_TYPES.items():
        n = failure_counts.get(cls, 0)
        lines.append(f"| {cls} | {n:,} | {n/total_frames:.1%} |")

    lines += [
        "",
        "---",
        "",
        "## Phase 1 — Category Results",
        "",
        "| Category | Cases | Prod Acc | Shadow Acc | False-Target Rate |",
        "|----------|-------|----------|------------|-------------------|",
    ]
    for cat, d in sorted(cats.items()):
        lines.append(
            f"| {cat} | {d['n']} | {mean(d['prod_acc']):.1%} | "
            f"{mean(d['shadow_acc']):.1%} | {mean(d['false']):.1%} |"
        )

    lines += [
        "",
        "---",
        "",
        "## Phase 3 — Shadow Intent Engine Architecture",
        "",
        "The Phase 1 prototype uses three heuristics:",
        "",
        "1. **Slide extrapolation:** when `pitchVelocity > 0.8 MIDI/sec`, project pitch",
        "   direction 150ms ahead and snap to the nearest scale note.",
        "2. **Stability anchor:** when `stabilityDuration > 80ms` and `confidence > 0.55`,",
        "   commit to the current nearest scale note with high confidence.",
        "3. **Preservation:** when drifting from a stable note with low velocity, hold the",
        "   previous stable note rather than chasing small pitch deviations.",
        "",
        "**Known weaknesses in Phase 1 prototype:**",
        "- No register awareness (octave confusion on low-confidence frames)",
        "- Slide extrapolation tuned only for ±1-2 semitone intervals",
        "- No chromatic passing-tone suppression",
        "- No key-change handling",
        "",
        "---",
        "",
        "## Phase 5 — Adversarial Search Summary",
        "",
        f"Adversarial cases generated around: boundary, vibrato_boundary, semitone_slide.",
        f"Shadow improvement distribution: "
        f"min={min(shadow_improvements):.1%} "
        f"median={sorted(shadow_improvements)[len(shadow_improvements)//2]:.1%} "
        f"max={max(shadow_improvements):.1%}" if shadow_improvements else "N/A",
        "",
        "---",
        "",
        "## Recommended Architecture (Phase 3→Production Path)",
        "",
        "1. **Input features:** detectedMidi, pitchVelocity, stabilityDuration,",
        "   prevStableMidi, pitchDirection, candidateDistance, confidence, key/scale,",
        "   targetHistory (last 5), Strategy G state (hysteresisAge).",
        "2. **Core logic:** three-tier decision: SLIDE → STABLE → PRESERVE.",
        "   Confidence threshold gates aggressiveness.",
        "3. **Shadow mode first** (Phases 3-4): compare against production per frame.",
        "   Gate: shadow accuracy ≥ prod on 533+ regression cases, 0 octave regressions.",
        "4. **Active audio only after gate passes.**",
        "",
        "**Strategy G correction dynamics: FROZEN. Do not touch.**",
    ]

    report_path = OUT / "INTENT_ENGINE_BASELINE_REPORT.md"
    report_path.write_text("\n".join(lines))
    print(f"\nReport written: {report_path}")


if __name__ == "__main__":
    main()
