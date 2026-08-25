#!/usr/bin/env python3
"""
IntentBench v3 — Intent Engine Phase 3: Real-Motion + Shadow Validation

Phases executed in order:
  1. Real YIN motion analysis — 4 CME configs vs. existing corpus traces
  2. Estimator selection — synthetic (MotionBench) + real-vocal stability
  3. shadow_v3() — SLIDE_DESTINATION_V2 in trace only (no audio change)
  4. Full IntentBench — production vs v2 vs v3 shadow, all 90+ cases
  5. Adversarial motion analysis — denser tests for improved/regressed cats
  6. GO/HOLD gate

Constraints (DO NOT relax without explicit user sign-off):
  - kSlideDestinationV2Enabled stays FALSE in C++
  - No modification to Strategy G
  - All SLIDE_DESTINATION_V2 decisions are shadow/trace only
"""

import json
import math
import statistics
import struct
import subprocess
import sys
import wave
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Paths + constants
# ---------------------------------------------------------------------------
REPO       = Path(__file__).resolve().parent.parent
BENCH      = REPO / "build" / "plugins" / "auto-pitch" / "AutoPitchBench"
V2_OUT     = REPO / "vocalbench-results" / "intent-bench-v2"
OUT        = REPO / "vocalbench-results" / "intent-bench-v3"
SR         = 44100
BLOCK      = 512
BLOCK_SEC  = BLOCK / SR

OUT.mkdir(parents=True, exist_ok=True)
(OUT / "corpus").mkdir(exist_ok=True)
(OUT / "adversarial").mkdir(exist_ok=True)
(OUT / "traces").mkdir(exist_ok=True)

# Phase 2: MotionBench synthetic winner — FD@20ms (1.788 MIDI/sec RMSE)
SYNTH_WINNER_EST    = "FD"
SYNTH_WINNER_WIN    = 0.020
MOTION_RMSE_THRESH  = 2.0   # MIDI/sec — passed in Phase 3

# ---------------------------------------------------------------------------
# Note roles + Reason codes
# ---------------------------------------------------------------------------
class Role:
    STABLE_TARGET      = "STABLE_TARGET"
    TRANSITION         = "TRANSITION"
    PASSING_TONE       = "PASSING_TONE"
    SCOOP              = "SCOOP"
    SLIDE              = "SLIDE"
    VIBRATO_EXCURSION  = "VIBRATO_EXCURSION"
    WRONG_NOTE         = "WRONG_NOTE"
    RELEASE            = "RELEASE"

class Reason:
    NEAREST_STABLE           = "NEAREST_STABLE"
    DIRECTIONAL_DESTINATION  = "DIRECTIONAL_DESTINATION"
    PASSING_TONE             = "PASSING_TONE"
    SLIDE_DESTINATION        = "SLIDE_DESTINATION"
    STABILITY_ANCHOR         = "STABILITY_ANCHOR"
    VIBRATO_HOLD             = "VIBRATO_HOLD"
    SLIDE_DESTINATION_V2     = "SLIDE_DESTINATION_V2"
    LOW_CONFIDENCE_FALLBACK  = "LOW_CONFIDENCE_FALLBACK"

# ---------------------------------------------------------------------------
# Audio helpers (identical to v2)
# ---------------------------------------------------------------------------
def midi_to_hz(m):
    return 440.0 * (2.0 ** ((m - 69.0) / 12.0))

def write_wav(path, samples, sr=SR):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'w') as wf:
        wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(sr)
        wf.writeframes(struct.pack(f'<{len(samples)}h',
            *[max(-32767, min(32767, int(s * 32767))) for s in samples]))

def sine(hz, dur, amp=0.7, sr=SR, p0=0.0):
    n = int(dur * sr); out, p = [], p0
    inc = 2 * math.pi * hz / sr
    for _ in range(n):
        out.append(amp * math.sin(p)); p += inc
    return out, p % (2 * math.pi)

def slide(hz0, hz1, dur, amp=0.7, sr=SR, p0=0.0):
    n = int(dur * sr); out, p = [], p0
    for i in range(n):
        hz = hz0 * (hz1 / hz0) ** (i / n)
        out.append(amp * math.sin(p)); p += 2 * math.pi * hz / sr
    return out, p % (2 * math.pi)

def vibrato(center_hz, depth_c, rate_hz, dur, amp=0.7, sr=SR, p0=0.0):
    n = int(dur * sr); out, p, lp = [], p0, 0.0
    li = 2 * math.pi * rate_hz / sr
    for _ in range(n):
        hz = center_hz * (2 ** (math.sin(lp) * depth_c / 1200))
        out.append(amp * math.sin(p))
        p += 2 * math.pi * hz / sr; lp += li
    return out, p % (2 * math.pi)

def silence(dur, sr=SR):
    return [0.0] * int(dur * sr)

def env(s, atk=0.01, rel=0.02, sr=SR):
    s = list(s); n = len(s)
    na, nr = min(int(atk*sr), n), min(int(rel*sr), n)
    for i in range(na): s[i] *= i / na
    for i in range(nr): s[n-1-i] *= i / nr
    return s

# ---------------------------------------------------------------------------
# Scale helpers (identical to v2)
# ---------------------------------------------------------------------------
KEY_INTERVALS = {
    "major":     [0,2,4,5,7,9,11],
    "minor":     [0,2,3,5,7,8,10],
    "chromatic": list(range(12)),
}
ROOT_MAP = {"C":0,"C#":1,"Db":1,"D":2,"D#":3,"Eb":3,"E":4,"F":5,
            "F#":6,"Gb":6,"G":7,"G#":8,"Ab":8,"A":9,"A#":10,"Bb":10,"B":11}

def nearest_scale(midi_f, key, scale):
    root = ROOT_MAP.get(key, 9)
    ivs  = KEY_INTERVALS.get(scale, KEY_INTERVALS["minor"])
    best, bd = round(midi_f), 999.0
    for c in range(int(midi_f)-13, int(midi_f)+14):
        if ((c - root) % 12) in ivs:
            d = abs(c - midi_f)
            if d < bd: bd, best = d, c
    return best

def in_scale(midi_int, key, scale):
    root = ROOT_MAP.get(key, 9)
    ivs  = KEY_INTERVALS.get(scale, KEY_INTERVALS["minor"])
    return ((midi_int - root) % 12) in ivs

# ---------------------------------------------------------------------------
# Corpus (identical to v2)
# ---------------------------------------------------------------------------
A3,B3,C4,D4,E4,F4,G4,A4 = 57,59,60,62,64,65,67,69

@dataclass
class IntentSegment:
    t_start: float; t_end: float
    performed_note: float; intended_destination: int
    note_role: str; should_correct_now: bool

def make_corpus():
    cases = []

    for off_c, tag in [(-40,"m40"),(-50,"m50"),(-60,"m60"),(+40,"p40"),(+50,"p50"),(+60,"p60")]:
        hz = midi_to_hz(A3 + off_c/100)
        def _gen(h=hz): s,_ = sine(h, 2.5); return env(s)
        cases.append(dict(id=f"BC_A3_{tag}", category="boundary",
            key="A", scale="minor", gen=_gen,
            intent=[IntentSegment(0.0,2.5, A3+off_c/100, A3, Role.STABLE_TARGET, True)]))

    for dur_ms in [100, 150, 200, 300, 400]:
        pre_dur=0.5; post_dur=1.0; sl_dur=dur_ms/1000
        def _gen(sd=sl_dur):
            a,p = sine(midi_to_hz(A3), pre_dur)
            b,p = slide(midi_to_hz(A3), midi_to_hz(B3), sd, p0=p)
            c,_ = sine(midi_to_hz(B3), post_dur, p0=p)
            return env(a+b+c)
        cases.append(dict(id=f"SLIDE_semi_{dur_ms}ms", category="semitone_slide",
            key="A", scale="minor", gen=_gen,
            intent=[
                IntentSegment(0.0, pre_dur, A3, A3, Role.STABLE_TARGET, True),
                IntentSegment(pre_dur, pre_dur+sl_dur, A3+0.5, B3, Role.SLIDE, False),
                IntentSegment(pre_dur+sl_dur, pre_dur+sl_dur+post_dur, B3, B3, Role.STABLE_TARGET, True),
            ]))

    for dur_ms in [200, 400]:
        pre_dur=0.4; post_dur=1.0; sl_dur=dur_ms/1000
        def _gen(sd=sl_dur):
            a,p = sine(midi_to_hz(A3), pre_dur)
            b,p = slide(midi_to_hz(A3), midi_to_hz(D4), sd, p0=p)
            c,_ = sine(midi_to_hz(D4), post_dur, p0=p)
            return env(a+b+c)
        cases.append(dict(id=f"SLIDE_whole_{dur_ms}ms", category="whole_tone_slide",
            key="A", scale="minor", gen=_gen,
            intent=[
                IntentSegment(0.0, pre_dur, A3, A3, Role.STABLE_TARGET, True),
                IntentSegment(pre_dur, pre_dur+sl_dur, A3+2.5, D4, Role.SLIDE, False),
                IntentSegment(pre_dur+sl_dur, pre_dur+sl_dur+post_dur, D4, D4, Role.STABLE_TARGET, True),
            ]))

    for depth_c in [30, 60, 100]:
        sl_dur=0.15; post_dur=1.5
        def _gen(sh=midi_to_hz(A3-depth_c/100)):
            b,p = slide(sh, midi_to_hz(A3), sl_dur)
            c,_ = sine(midi_to_hz(A3), post_dur, p0=p)
            return env(b+c, atk=0.001)
        cases.append(dict(id=f"SCOOP_A3_{depth_c}c", category="scoop",
            key="A", scale="minor", gen=_gen,
            intent=[
                IntentSegment(0.0, sl_dur, A3-depth_c/100, A3, Role.SCOOP, False),
                IntentSegment(sl_dur, sl_dur+post_dur, A3, A3, Role.STABLE_TARGET, True),
            ]))

    for bb_dur_ms in [60, 100, 150, 200]:
        pre=0.4; bb=bb_dur_ms/1000; post=0.8
        def _gen(bd=bb):
            a,p = sine(midi_to_hz(A3), pre)
            b,p = sine(midi_to_hz(A3+1), bd, p0=p)
            c,_ = sine(midi_to_hz(B3), post, p0=p)
            return env(a+b+c, atk=0.005)
        cases.append(dict(id=f"CHROM_pass_Bb3_{bb_dur_ms}ms", category="chromatic_passing",
            key="A", scale="minor", gen=_gen,
            intent=[
                IntentSegment(0.0, pre, A3, B3, Role.STABLE_TARGET, True),
                IntentSegment(pre, pre+bb, A3+1, B3, Role.PASSING_TONE, False),
                IntentSegment(pre+bb, pre+bb+post, B3, B3, Role.STABLE_TARGET, True),
            ]))

    for depth_c in [50, 80]:
        pre=0.6; bend=0.25; post=0.8
        def _gen(dc=depth_c):
            a,p = sine(midi_to_hz(G4-12), pre)
            b,p = slide(midi_to_hz(G4-12), midi_to_hz(G4-12+dc/100), bend, p0=p)
            c,_ = sine(midi_to_hz(G4-12), post, p0=p)
            return env(a+b+c)
        cases.append(dict(id=f"OOS_blues_G3_bend{depth_c}c", category="out_of_scale",
            key="A", scale="minor", gen=_gen,
            intent=[
                IntentSegment(0.0, pre, G4-12, G4-12, Role.STABLE_TARGET, True),
                IntentSegment(pre, pre+bend, G4-12+depth_c/100, G4-12, Role.VIBRATO_EXCURSION, False),
                IntentSegment(pre+bend, pre+bend+post, G4-12, G4-12, Role.STABLE_TARGET, True),
            ]))

    for wrong_dur_ms in [80, 150]:
        wd = wrong_dur_ms/1000
        def _gen(d=wd):
            a,p = sine(midi_to_hz(A3), 0.5)
            b,p = sine(midi_to_hz(A3+2), d, p0=p)
            c,_ = sine(midi_to_hz(A3), 0.8, p0=p)
            return env(a+b+c, atk=0.005, rel=0.01)
        cases.append(dict(id=f"WRONG_note_{wrong_dur_ms}ms", category="wrong_note",
            key="A", scale="minor", gen=_gen,
            intent=[
                IntentSegment(0.0, 0.5, A3, A3, Role.STABLE_TARGET, True),
                IntentSegment(0.5, 0.5+wd, A3+2, A3, Role.WRONG_NOTE, True),
                IntentSegment(0.5+wd, 1.3+wd, A3, A3, Role.STABLE_TARGET, True),
            ]))

    for depth_c in [30, 50, 70]:
        center = A3 + 0.5
        def _gen(d=depth_c, c=center):
            s,_ = vibrato(midi_to_hz(c), d, 5.0, 3.0)
            return env(s)
        cases.append(dict(id=f"VIB_cross_{depth_c}c", category="vibrato_boundary",
            key="A", scale="minor", gen=_gen,
            intent=[IntentSegment(0.0, 3.0, center, A3, Role.VIBRATO_EXCURSION, False)]))

    run_notes = [A3, B3, C4, D4, E4]
    for nd_ms in [80, 120, 200, 350]:
        def _gen(nd=nd_ms/1000):
            out=[]; p=0.0
            for note in run_notes:
                s,p = sine(midi_to_hz(note), nd, p0=p)
                out += env(s, atk=0.005, rel=0.005)
            return out
        intents=[]; t=0.0
        for idx, note in enumerate(run_notes):
            role = Role.STABLE_TARGET if idx == len(run_notes)-1 else Role.PASSING_TONE
            intents.append(IntentSegment(t, t+nd_ms/1000, note, E4, role, idx==len(run_notes)-1))
            t += nd_ms/1000
        cases.append(dict(id=f"RUN_up_{nd_ms}ms", category="run",
            key="A", scale="minor", gen=_gen, intent=intents))

    for n_reps in [4, 8]:
        def _gen(nr=n_reps):
            out=[]; p=0.0
            for _ in range(nr):
                s,p = sine(midi_to_hz(A3), 0.15, p0=p)
                out += env(s, atk=0.008, rel=0.008)
                out += silence(0.04)
            return out
        cases.append(dict(id=f"REPEAT_A3_{n_reps}x", category="repeated_note",
            key="A", scale="minor", gen=_gen,
            intent=[IntentSegment(0.0, 9.9, A3, A3, Role.STABLE_TARGET, True)]))

    for dur_ms in [80, 100, 150, 200, 350, 500]:
        def _gen(d=dur_ms/1000):
            s,_ = sine(midi_to_hz(A3), d)
            return env(s, atk=0.01, rel=0.02)
        cases.append(dict(id=f"SHORT_A3_{dur_ms}ms", category="short_note",
            key="A", scale="minor", gen=_gen,
            intent=[IntentSegment(0.0, dur_ms/1000, A3, A3, Role.STABLE_TARGET, True)]))

    for depth_c in [50, 100]:
        def _gen(dc=depth_c):
            a,p = sine(midi_to_hz(A3), 1.0)
            b,_ = slide(midi_to_hz(A3), midi_to_hz(A3-dc/100), 0.3, amp=0.5, p0=p)
            return env(a+b, rel=0.05)
        cases.append(dict(id=f"FALL_A3_{depth_c}c", category="fall",
            key="A", scale="minor", gen=_gen,
            intent=[
                IntentSegment(0.0, 1.0, A3, A3, Role.STABLE_TARGET, True),
                IntentSegment(1.0, 1.3, A3-depth_c/100, A3, Role.RELEASE, False),
            ]))

    return cases


def make_adversarial(cases):
    adv = []; seen = set()
    for case in cases:
        cat = case["category"]; key = case["key"]; scale = case["scale"]

        if cat == "boundary":
            for off in [-58,-55,-52,-48,-45,+45,+48,+52,+55,+58]:
                uid = f"ADV_BC_{off:+d}c"
                if uid in seen: continue
                seen.add(uid)
                hz = midi_to_hz(A3 + off/100)
                def _gen(h=hz): s,_ = sine(h, 2.0); return env(s)
                adv.append(dict(id=uid, category="adv_boundary", key=key, scale=scale, gen=_gen,
                    intent=[IntentSegment(0,2.0, A3+off/100, A3, Role.STABLE_TARGET, True)]))

        elif cat == "vibrato_boundary":
            for dep in [15,20,25,35,45,55,65,80]:
                uid = f"ADV_VIB_{dep}c"
                if uid in seen: continue
                seen.add(uid)
                def _gen(d=dep): s,_ = vibrato(midi_to_hz(A3+0.5), d, 5.0, 3.0); return env(s)
                adv.append(dict(id=uid, category="adv_vib_boundary", key=key, scale=scale, gen=_gen,
                    intent=[IntentSegment(0,3.0, A3+0.5, A3, Role.VIBRATO_EXCURSION, False)]))

        elif cat == "semitone_slide":
            for d_ms in [50,75,100,150,200,300,500,750]:
                for start_off in [-20, 0, +20]:
                    uid = f"ADV_SLIDE_semi_{d_ms}ms_off{start_off:+d}c"
                    if uid in seen: continue
                    seen.add(uid); sd=d_ms/1000
                    def _gen(s=sd, so=start_off):
                        a,p = sine(midi_to_hz(A3+so/100), 0.4)
                        b,p = slide(midi_to_hz(A3+so/100), midi_to_hz(B3), s, p0=p)
                        c,_ = sine(midi_to_hz(B3), 0.8, p0=p)
                        return env(a+b+c)
                    adv.append(dict(id=uid, category="adv_slide", key=key, scale=scale, gen=_gen,
                        intent=[
                            IntentSegment(0, 0.4, A3+start_off/100, B3, Role.STABLE_TARGET, True),
                            IntentSegment(0.4, 0.4+sd, A3+0.5, B3, Role.SLIDE, False),
                            IntentSegment(0.4+sd, 9.9, B3, B3, Role.STABLE_TARGET, True),
                        ]))

        elif cat == "chromatic_passing":
            for bb_ms in [40,60,80,100,120,160,200]:
                uid = f"ADV_CHROM_{bb_ms}ms"
                if uid in seen: continue
                seen.add(uid); bd=bb_ms/1000
                def _gen(d=bd):
                    a,p = sine(midi_to_hz(A3), 0.35)
                    b,p = sine(midi_to_hz(A3+1), d, p0=p)
                    c,_ = sine(midi_to_hz(B3), 0.7, p0=p)
                    return env(a+b+c)
                adv.append(dict(id=uid, category="adv_chromatic_pass", key=key, scale=scale, gen=_gen,
                    intent=[
                        IntentSegment(0, 0.35, A3, B3, Role.STABLE_TARGET, True),
                        IntentSegment(0.35, 0.35+bd, A3+1, B3, Role.PASSING_TONE, False),
                        IntentSegment(0.35+bd, 9.9, B3, B3, Role.STABLE_TARGET, True),
                    ]))
    return adv

# ---------------------------------------------------------------------------
# Phase 1 — Python ContinuousMotionEstimator (exact port of C++ CME)
# ---------------------------------------------------------------------------
class ContinuousMotionEstimator:
    CAPACITY = 64

    def __init__(self):
        self.reset()

    def reset(self):
        self._midi = [0.0] * self.CAPACITY
        self._time = [0.0] * self.CAPACITY
        self._pos = 0
        self._fill = 0
        self._cum_time = 0.0

    def push(self, freq_hz: float, block_sec: float):
        if freq_hz <= 0.0 or block_sec <= 0.0:
            return
        self._cum_time += block_sec
        mf = 69.0 + 12.0 * math.log2(freq_hz / 440.0)
        self._midi[self._pos] = mf
        self._time[self._pos] = self._cum_time
        self._pos = (self._pos + 1) % self.CAPACITY
        if self._fill < self.CAPACITY:
            self._fill += 1

    def _read_window(self, window_sec: float):
        if self._fill == 0:
            return [], []
        last = (self._pos - 1 + self.CAPACITY) % self.CAPACITY
        t_now = self._time[last]
        t_cut = t_now - window_sec
        tmp_m, tmp_t = [], []
        for i in range(self._fill):
            idx = (self._pos - 1 - i + self.CAPACITY * 2) % self.CAPACITY
            if self._time[idx] < t_cut:
                break
            tmp_m.append(self._midi[idx])
            tmp_t.append(self._time[idx])
        tmp_m.reverse(); tmp_t.reverse()
        return tmp_m, tmp_t

    @staticmethod
    def _fd(m, t):
        if len(m) < 2: return 0.0
        dt = t[-1] - t[-2]
        return (m[-1] - m[-2]) / dt if dt > 1e-12 else 0.0

    @staticmethod
    def _mha(m, t):
        if len(m) < 2: return 0.0
        slopes = [(m[i]-m[i-1])/(t[i]-t[i-1]) for i in range(1,len(m)) if t[i]-t[i-1]>1e-12]
        return sum(slopes)/len(slopes) if slopes else 0.0

    @staticmethod
    def _lsq(m, t):
        if len(m) < 2: return 0.0
        n = len(m); t0 = t[0]; xs = [ti-t0 for ti in t]
        sx=sum(xs); sy=sum(m); sxx=sum(x*x for x in xs); sxy=sum(x*y for x,y in zip(xs,m))
        d = n*sxx - sx*sx
        return (n*sxy - sx*sy)/d if abs(d) > 1e-15 else 0.0

    @staticmethod
    def _rm(m, t):
        if len(m) < 2: return 0.0
        slopes = sorted((m[i]-m[i-1])/(t[i]-t[i-1]) for i in range(1,len(m)) if t[i]-t[i-1]>1e-12)
        if not slopes: return 0.0
        n = len(slopes)
        return slopes[n//2] if n%2 else 0.5*(slopes[n//2-1]+slopes[n//2])

    @staticmethod
    def _cw(m, t):
        if len(m) < 2: return 0.0
        ls = ContinuousMotionEstimator._lsq(m, t)
        med = ContinuousMotionEstimator._rm(m, t)
        diff = abs(ls - med)
        w = 0.70 if diff < 2.0 else 0.30
        return w*ls + (1.0-w)*med

    def analyse(self, window_sec=0.020, estimator="FD"):
        out = {"midiFloat":0.0,"velMidi":0.0,"velCents":0.0,
               "direction":0.0,"confidence":0.0,"accel":0.0,"isVibrato":False}
        if self._fill < 2: return out

        last = (self._pos - 1 + self.CAPACITY) % self.CAPACITY
        out["midiFloat"] = self._midi[last]

        m, t = self._read_window(window_sec)
        if len(m) < 2: return out

        ls = self._lsq(m, t); med = self._rm(m, t)
        diff = abs(ls - med)
        fns = {"FD":self._fd,"MHA":self._mha,"LSQ":lambda m,t: ls,
               "RM":lambda m,t: med,"CW":self._cw}
        vel = fns.get(estimator, self._lsq)(m, t)

        out["velMidi"]    = vel
        out["velCents"]   = vel * 100.0
        out["direction"]  = 1.0 if vel > 0.3 else (-1.0 if vel < -0.3 else 0.0)
        out["confidence"] = max(0.0, 1.0 - diff / 10.0)

        # Acceleration: inner half vs full window slope
        if len(m) >= 4:
            mh, th = self._read_window(window_sec * 0.5)
            if len(mh) >= 2:
                hw = window_sec * 0.5
                if hw > 1e-9:
                    out["accel"] = (self._lsq(mh,th) - ls) / hw

        # Vibrato: sign reversal in adjacent slopes
        has_pos = has_neg = False
        for i in range(1, len(m)):
            dt = t[i]-t[i-1]
            if dt < 1e-12: continue
            v = (m[i]-m[i-1])/dt
            if v >  1.5: has_pos = True
            if v < -1.5: has_neg = True
        out["isVibrato"] = has_pos and has_neg
        return out

# 4 configs evaluated in Phase 1
CME_CONFIGS = [
    ("A", "FD",  0.020),   # Phase-3 MotionBench winner
    ("B", "CW",  0.040),
    ("C", "LSQ", 0.040),
    ("D", "RM",  0.040),
]

# ---------------------------------------------------------------------------
# Bench runner — loads existing v2 trace or runs bench fresh
# ---------------------------------------------------------------------------
def load_or_run_bench(case_id, wav_path, key, scale, category):
    v2_dir = "adversarial" if category.startswith("adv_") else "corpus"
    v2_trace = V2_OUT / "traces" / f"{case_id}_trace.json"

    if v2_trace.exists():
        data = json.loads(v2_trace.read_text())
        return data.get("frames", [])

    # Run bench fresh if v2 trace missing
    tr = OUT / "traces" / f"{case_id}_trace.json"
    wo = OUT / "traces" / f"{case_id}_out.wav"
    r = subprocess.run([
        str(BENCH), str(wav_path),
        "--output", str(wo), "--trace", str(tr),
        "--key", key, "--scale", scale,
        "--amount", "0.82", "--speed", "0.70", "--block", str(BLOCK),
    ], capture_output=True, text=True)
    if r.returncode != 0 or not tr.exists():
        return None
    return json.loads(tr.read_text()).get("frames", [])

# ---------------------------------------------------------------------------
# GT labeling + enrichment (identical to v2)
# ---------------------------------------------------------------------------
def label_frames(frames, intent_segs):
    for fr in frames:
        t = fr.get("timeSec", 0.0)
        seg = None
        for s in intent_segs:
            if s.t_start <= t < s.t_end:
                seg = s; break
        if seg is None and intent_segs:
            seg = intent_segs[-1]
        fr["gtPerformedNote"]    = seg.performed_note if seg else None
        fr["gtIntendedDest"]     = seg.intended_destination if seg else None
        fr["gtNoteRole"]         = seg.note_role if seg else None
        fr["gtShouldCorrectNow"] = seg.should_correct_now if seg else False
    return frames

def enrich_frames(frames):
    prev_tgt = None; stab = 0
    for i, fr in enumerate(frames):
        det = fr.get("detectedMidi", -1) or -1
        pv = 0.0
        if i > 0 and frames[i-1].get("voiced") and fr.get("voiced"):
            pv = abs(det - (frames[i-1].get("detectedMidi") or det)) / BLOCK_SEC
        fr["pitchVelocity"] = round(pv, 3)
        tgt = det
        if tgt != prev_tgt: stab=0; prev_tgt=tgt
        else: stab += 1
        fr["targetMidi"]       = det
        fr["targetStabFrames"] = stab
        fr["targetStabSec"]    = round(stab * BLOCK_SEC, 4)
    return frames

# ---------------------------------------------------------------------------
# Phase 1 — enrich frames with CME output for all 4 configs
# ---------------------------------------------------------------------------
def enrich_with_cme(frames):
    """Adds cme_{tag}_vel, cme_{tag}_conf, cme_{tag}_vibrato per frame."""
    cmes = {tag: ContinuousMotionEstimator() for tag,_,_ in CME_CONFIGS}

    for fr in frames:
        if not fr.get("voiced"):
            for tag,_,_ in CME_CONFIGS:
                cmes[tag].reset()
                fr[f"cme_{tag}_vel"]     = 0.0
                fr[f"cme_{tag}_conf"]    = 0.0
                fr[f"cme_{tag}_vibrato"] = False
                fr[f"cme_{tag}_accel"]   = 0.0
            continue

        # Prefer detectedHz (double-precision from bench); fall back to detectedMidi
        det_hz = fr.get("detectedHz")
        if not det_hz or det_hz <= 0:
            det_midi = fr.get("detectedMidi", -1) or -1
            det_hz = midi_to_hz(det_midi) if det_midi > 0 else 0.0

        for tag, est, win in CME_CONFIGS:
            if det_hz > 0:
                cmes[tag].push(det_hz, BLOCK_SEC)
            mf = cmes[tag].analyse(window_sec=win, estimator=est)
            fr[f"cme_{tag}_vel"]     = mf["velMidi"]
            fr[f"cme_{tag}_conf"]    = mf["confidence"]
            fr[f"cme_{tag}_vibrato"] = mf["isVibrato"]
            fr[f"cme_{tag}_accel"]   = mf["accel"]
            fr[f"cme_{tag}_midi"]    = mf["midiFloat"]

    return frames

# ---------------------------------------------------------------------------
# Phase 1 — aggregate CME config metrics by GT region
# ---------------------------------------------------------------------------
def analyze_cme_configs(all_results):
    """
    Returns dict[config_tag] -> {noise, flip_rate, accel_spikes, false_slide, vibrato_miss}
    """
    config_stats = {tag: {"vel_stable":[], "flip_stable":[], "accel_all":[],
                          "false_slide":[], "vibrato_vib":[], "vibrato_stable":[]}
                    for tag,_,_ in CME_CONFIGS}

    for result in all_results:
        frames = result["frames"]
        voiced = [f for f in frames if f.get("voiced") and f.get("gtNoteRole")]

        stable   = [f for f in voiced if f["gtNoteRole"] == Role.STABLE_TARGET]
        vib_frs  = [f for f in voiced if f["gtNoteRole"] == Role.VIBRATO_EXCURSION]

        for tag,_,_ in CME_CONFIGS:
            vk  = f"cme_{tag}_vel"
            ck  = f"cme_{tag}_conf"
            ak  = f"cme_{tag}_accel"
            vbk = f"cme_{tag}_vibrato"

            # velocity noise in stable regions (std dev of velocity)
            vels_stable = [f[vk] for f in stable if vk in f]
            if vels_stable:
                config_stats[tag]["vel_stable"].extend(vels_stable)

            # direction flip rate in stable regions
            for i in range(1, len(stable)):
                if vk in stable[i] and vk in stable[i-1]:
                    v_cur = stable[i][vk]; v_prev = stable[i-1][vk]
                    flip = (v_cur > 0.3 and v_prev < -0.3) or (v_cur < -0.3 and v_prev > 0.3)
                    config_stats[tag]["flip_stable"].append(1.0 if flip else 0.0)

            # acceleration spikes across all voiced
            accels = [abs(f[ak]) for f in voiced if ak in f]
            config_stats[tag]["accel_all"].extend(accels)

            # false slide detection: stable frames misclassified as sliding (|vel| > 3.0)
            for f in stable:
                if vk in f:
                    config_stats[tag]["false_slide"].append(1.0 if abs(f[vk]) > 3.0 else 0.0)

            # vibrato detection accuracy in vibrato excursion regions
            for f in vib_frs:
                if vbk in f:
                    config_stats[tag]["vibrato_vib"].append(1.0 if f[vbk] else 0.0)

            # vibrato false positive in stable regions
            for f in stable:
                if vbk in f:
                    config_stats[tag]["vibrato_stable"].append(1.0 if f[vbk] else 0.0)

    report = {}
    for tag,_,_ in CME_CONFIGS:
        s = config_stats[tag]
        def mean(lst): return sum(lst)/len(lst) if lst else 0.0
        def std(lst):
            if len(lst) < 2: return 0.0
            m = mean(lst); return math.sqrt(sum((x-m)**2 for x in lst)/(len(lst)-1))
        accel_all = s["accel_all"]
        report[tag] = {
            "vel_noise_std":     round(std(s["vel_stable"]), 4),
            "vel_mean_stable":   round(abs(mean(s["vel_stable"])), 4),
            "flip_rate":         round(mean(s["flip_stable"]), 4),
            "accel_spike_rate":  round(mean([1.0 if a>10.0 else 0.0 for a in accel_all]), 4),
            "false_slide_rate":  round(mean(s["false_slide"]), 4),
            "vibrato_recall":    round(mean(s["vibrato_vib"]), 4),
            "vibrato_fp_rate":   round(mean(s["vibrato_stable"]), 4),
            "n_stable":          len(s["vel_stable"]),
        }
    return report

def select_best_config(cme_report):
    """
    Phase 2: combine MotionBench synthetic result with real-vocal stability.
    Score each config; return (tag, est, window_sec).
    """
    # Synthetic winner gets a baseline score advantage
    synth_bonus = {"A": 2.0, "B": 0.0, "C": 0.0, "D": 0.0}  # A = FD@20ms

    scores = {}
    for tag,_,_ in CME_CONFIGS:
        m = cme_report[tag]
        # Lower is better for all except vibrato_recall
        s = 0.0
        s -= m["vel_noise_std"]   * 3.0    # biggest weight — fundamental noise metric
        s -= m["false_slide_rate"] * 5.0   # critical: false slide triggers false targeting
        s -= m["flip_rate"]        * 2.0
        s -= m["accel_spike_rate"] * 1.0
        s += m["vibrato_recall"]   * 1.0   # recall good, fp rate bad
        s -= m["vibrato_fp_rate"]  * 1.5
        s += synth_bonus[tag]
        scores[tag] = round(s, 4)

    best_tag = max(scores, key=lambda t: scores[t])
    config_map = {tag: (est, win) for tag,est,win in CME_CONFIGS}
    est, win = config_map[best_tag]
    return best_tag, est, win, scores

# ---------------------------------------------------------------------------
# Intent Engine v2 shadow (identical to v2 script)
# ---------------------------------------------------------------------------
HIST_LEN   = 12
SLIDE_VEL  = 0.6
PASS_DWELL = 0.18
STAB_SEC   = 0.10
PERSIST_SEC= 0.12

@dataclass
class IntentV2State:
    pitch_hist:    deque = field(default_factory=lambda: deque(maxlen=HIST_LEN))
    vel_hist:      deque = field(default_factory=lambda: deque(maxlen=HIST_LEN))
    stable_target: int   = -1
    stable_sec:    float = 0.0
    last_voiced:   float = -1.0
    dwell:         dict  = field(default_factory=dict)
    prod_target:   int   = -1

    def smoothed_velocity(self):
        pitches = list(self.pitch_hist)
        if len(pitches) < 2: return 0.0
        return (pitches[-1] - pitches[0]) / (len(pitches) * BLOCK_SEC)

    def is_directionally_consistent(self, frames=6):
        pvs = list(self.vel_hist)[-frames:]
        if len(pvs) < 3: return False, 0.0
        signs = [1 if v>0.3 else (-1 if v<-0.3 else 0) for v in pvs]
        pos=signs.count(1); neg=signs.count(-1)
        if pos >= len(signs)*0.6: return True, 1.0
        if neg >= len(signs)*0.6: return True, -1.0
        return False, 0.0

def shadow_v2(frames, key, scale):
    state = IntentV2State()
    for i, fr in enumerate(frames):
        if not fr.get("voiced"):
            fr.update(shadowV2Target=-1, shadowV2Confidence=0.0,
                      shadowV2Reason=Reason.LOW_CONFIDENCE_FALLBACK)
            state.pitch_hist.clear(); state.vel_hist.clear()
            state.dwell.clear(); state.stable_sec=0.0; state.last_voiced=-1.0
            continue
        det  = float(fr.get("detectedMidi",-1) or -1)
        conf = float(fr.get("confidence",0.0) or 0.0)
        prod = fr.get("targetMidi", int(round(det)))
        state.prod_target = prod
        sv = (det-state.last_voiced)/BLOCK_SEC if state.last_voiced >= 0 else 0.0
        state.last_voiced = det
        state.pitch_hist.append(det); state.vel_hist.append(sv)
        smoothed_vel = state.smoothed_velocity()
        consistent, direction = state.is_directionally_consistent()
        near_note = nearest_scale(det, key, scale)
        state.dwell[near_note] = state.dwell.get(near_note,0) + 1
        target=prod; r_conf=conf*0.6; reason=Reason.LOW_CONFIDENCE_FALLBACK

        if abs(smoothed_vel) >= SLIDE_VEL and consistent:
            la = det + direction * abs(smoothed_vel) * 0.20
            destination = nearest_scale(la, key, scale)
            if destination == near_note and abs(smoothed_vel) > 1.0:
                la = det + direction * abs(smoothed_vel) * 0.35
                destination = nearest_scale(la, key, scale)
            target=destination; r_conf=min(0.88, conf*0.95); reason=Reason.SLIDE_DESTINATION
        elif (consistent and state.dwell.get(near_note,0)*BLOCK_SEC < PASS_DWELL
              and abs(smoothed_vel) > 0.35 and state.stable_target != -1):
            target=state.stable_target; r_conf=0.72; reason=Reason.PASSING_TONE
        elif (state.stable_sec >= STAB_SEC and conf >= 0.55 and abs(smoothed_vel) < 0.5):
            candidate = nearest_scale(det, key, scale)
            if (state.stable_target != -1 and state.stable_target != candidate
                    and abs(smoothed_vel) < 1.2 and state.stable_sec < PERSIST_SEC*3):
                target=state.stable_target; r_conf=0.80; reason=Reason.VIBRATO_HOLD
            else:
                target=candidate; state.stable_target=candidate
                r_conf=min(0.95,conf); reason=Reason.STABILITY_ANCHOR
        elif state.stable_target != -1 and abs(smoothed_vel) < 0.8:
            target=state.stable_target; r_conf=min(0.78,conf*1.1); reason=Reason.VIBRATO_HOLD
        else:
            target=nearest_scale(det,key,scale); r_conf=conf*0.70
            if r_conf >= 0.50: reason=Reason.NEAREST_STABLE
            else: target=prod; r_conf=conf*0.50

        if target == state.stable_target and abs(smoothed_vel) < 0.8:
            state.stable_sec += BLOCK_SEC
        elif reason not in (Reason.PASSING_TONE, Reason.VIBRATO_HOLD):
            state.stable_sec = 0.0

        fr.update(shadowV2Target=target, shadowV2Confidence=round(r_conf,3), shadowV2Reason=reason)
    return frames

# ---------------------------------------------------------------------------
# Phase 3 — shadow_v3: v2 + SLIDE_DESTINATION_V2 (trace/shadow only)
# ---------------------------------------------------------------------------
# Gate constants (mirror C++ PluginProcessor.cpp)
_V3_MIN_CONF         = 0.60   # detectedMidi confidence
_V3_MIN_MOTION_CONF  = 0.65   # CME confidence (LSQ/Median agreement)
_V3_MIN_VEL          = 3.0    # MIDI/sec — minimum slide velocity to trigger
_V3_EXTRAP_SEC       = 0.080  # seconds ahead to extrapolate
_V3_MAX_SLIDE_DIST   = 4.0    # MIDI — hard cap on extrapolated target

def shadow_v3(frames, key, scale, est="FD", window_sec=0.020):
    """
    Extends shadow_v2 with a pre-tier SLIDE_DESTINATION_V2 check.
    When fired, this reason code is written to shadowV3Reason.
    Tiers 1-5 of v2 run unchanged as fallback.

    All decisions are shadow/trace only — kSlideDestinationV2Enabled = false in C++.
    """
    state = IntentV2State()
    cme = ContinuousMotionEstimator()

    for i, fr in enumerate(frames):
        if not fr.get("voiced"):
            fr.update(shadowV3Target=-1, shadowV3Confidence=0.0,
                      shadowV3Reason=Reason.LOW_CONFIDENCE_FALLBACK)
            state.pitch_hist.clear(); state.vel_hist.clear()
            state.dwell.clear(); state.stable_sec=0.0; state.last_voiced=-1.0
            cme.reset()
            continue

        det  = float(fr.get("detectedMidi",-1) or -1)
        conf = float(fr.get("confidence",0.0) or 0.0)
        prod = fr.get("targetMidi", int(round(det)))
        state.prod_target = prod
        sv = (det-state.last_voiced)/BLOCK_SEC if state.last_voiced >= 0 else 0.0
        state.last_voiced = det
        state.pitch_hist.append(det); state.vel_hist.append(sv)
        smoothed_vel = state.smoothed_velocity()
        consistent, direction = state.is_directionally_consistent()
        near_note = nearest_scale(det, key, scale)
        state.dwell[near_note] = state.dwell.get(near_note,0) + 1

        # Push into CME
        det_hz = fr.get("detectedHz")
        if not det_hz or det_hz <= 0:
            det_hz = midi_to_hz(det) if det > 0 else 0.0
        if det_hz > 0:
            cme.push(det_hz, BLOCK_SEC)
        motion = cme.analyse(window_sec=window_sec, estimator=est)

        # Oscillating? (sign reversal in last 4 signed velocities → chaotic)
        recent_vels = list(state.vel_hist)[-4:]
        osc = (any(v > 0.3 for v in recent_vels) and any(v < -0.3 for v in recent_vels)
               and len(recent_vels) >= 4)

        target=prod; r_conf=conf*0.6; reason=Reason.LOW_CONFIDENCE_FALLBACK

        # --- Phase 4 tier: SLIDE_DESTINATION_V2 ---
        v2_fired = (
            not motion["isVibrato"]
            and consistent
            and conf >= _V3_MIN_CONF
            and motion["confidence"] >= _V3_MIN_MOTION_CONF
            and abs(motion["velMidi"]) >= _V3_MIN_VEL
            and not osc
        )
        if v2_fired:
            raw_dest = motion["midiFloat"] + motion["velMidi"] * _V3_EXTRAP_SEC
            bounded  = max(det - _V3_MAX_SLIDE_DIST, min(det + _V3_MAX_SLIDE_DIST, raw_dest))
            snap     = nearest_scale(bounded, key, scale)
            if snap != near_note and abs(snap - near_note) >= 1:
                target = snap
                r_conf = motion["confidence"] * 0.85
                reason = Reason.SLIDE_DESTINATION_V2

        # --- v2 tiers fallback (only if v2 did not fire) ---
        if reason == Reason.LOW_CONFIDENCE_FALLBACK:
            if abs(smoothed_vel) >= SLIDE_VEL and consistent:
                la = det + direction * abs(smoothed_vel) * 0.20
                destination = nearest_scale(la, key, scale)
                if destination == near_note and abs(smoothed_vel) > 1.0:
                    la = det + direction * abs(smoothed_vel) * 0.35
                    destination = nearest_scale(la, key, scale)
                target=destination; r_conf=min(0.88,conf*0.95); reason=Reason.SLIDE_DESTINATION
            elif (consistent and state.dwell.get(near_note,0)*BLOCK_SEC < PASS_DWELL
                  and abs(smoothed_vel) > 0.35 and state.stable_target != -1):
                target=state.stable_target; r_conf=0.72; reason=Reason.PASSING_TONE
            elif state.stable_sec >= STAB_SEC and conf >= 0.55 and abs(smoothed_vel) < 0.5:
                candidate = nearest_scale(det, key, scale)
                if (state.stable_target != -1 and state.stable_target != candidate
                        and abs(smoothed_vel) < 1.2 and state.stable_sec < PERSIST_SEC*3):
                    target=state.stable_target; r_conf=0.80; reason=Reason.VIBRATO_HOLD
                else:
                    target=candidate; state.stable_target=candidate
                    r_conf=min(0.95,conf); reason=Reason.STABILITY_ANCHOR
            elif state.stable_target != -1 and abs(smoothed_vel) < 0.8:
                target=state.stable_target; r_conf=min(0.78,conf*1.1); reason=Reason.VIBRATO_HOLD
            else:
                target=nearest_scale(det,key,scale); r_conf=conf*0.70
                if r_conf >= 0.50: reason=Reason.NEAREST_STABLE
                else: target=prod; r_conf=conf*0.50

        if target == state.stable_target and abs(smoothed_vel) < 0.8:
            state.stable_sec += BLOCK_SEC
        elif reason not in (Reason.PASSING_TONE, Reason.VIBRATO_HOLD, Reason.SLIDE_DESTINATION_V2):
            state.stable_sec = 0.0

        fr.update(shadowV3Target=target, shadowV3Confidence=round(r_conf,3), shadowV3Reason=reason)
    return frames

# ---------------------------------------------------------------------------
# Phase 4 — unified metrics (prod / v2 / v3)
# ---------------------------------------------------------------------------
def compute_metrics_v3(frames):
    voiced = [f for f in frames if f.get("voiced") and f.get("gtNoteRole")]
    if not voiced: return {}

    stable  = [f for f in voiced if f.get("gtNoteRole") == Role.STABLE_TARGET]
    transit = [f for f in voiced if f.get("gtNoteRole") in (Role.SLIDE, Role.SCOOP, Role.RELEASE)]
    passing = [f for f in voiced if f.get("gtNoteRole") in (Role.PASSING_TONE, Role.VIBRATO_EXCURSION)]
    pass_only = [f for f in voiced if f.get("gtNoteRole") == Role.PASSING_TONE]

    def acc(frs, key):
        if not frs: return None
        return round(sum(1 for f in frs if f.get(key) == f.get("gtIntendedDest")) / len(frs), 4)

    def pass_pres(frs, key):
        if not frs: return None
        return round(sum(1 for f in frs
                         if f.get(key) != round(f.get("gtPerformedNote") or 0)) / len(frs), 4)

    def ft(frs, key):
        should = [f for f in frs if f.get("gtShouldCorrectNow")]
        if not should: return None
        return round(sum(1 for f in should if f.get(key) != f.get("gtIntendedDest")) / len(should), 4)

    def chatter(frs, key):
        if len(frs) < 2: return 0.0
        return round(sum(1 for i in range(1,len(frs)) if frs[i].get(key)!=frs[i-1].get(key))/len(frs), 4)

    def oct_err(frs, key):
        return sum(1 for f in frs if abs((f.get(key) or 0)-(f.get("gtIntendedDest") or 0)) >= 12)

    return {
        "n_stable":              len(stable),
        "n_transition":          len(transit),
        "n_passing":             len(passing),
        "prod_stable_acc":       acc(stable,    "targetMidi"),
        "v2_stable_acc":         acc(stable,    "shadowV2Target"),
        "v3_stable_acc":         acc(stable,    "shadowV3Target"),
        "prod_dest_acc":         acc(transit,   "targetMidi"),
        "v2_dest_acc":           acc(transit,   "shadowV2Target"),
        "v3_dest_acc":           acc(transit,   "shadowV3Target"),
        "prod_pass_pres":        pass_pres(pass_only, "targetMidi"),
        "v2_pass_pres":          pass_pres(pass_only, "shadowV2Target"),
        "v3_pass_pres":          pass_pres(pass_only, "shadowV3Target"),
        "prod_false_target":     ft(stable+transit, "targetMidi"),
        "v2_false_target":       ft(stable+transit, "shadowV2Target"),
        "v3_false_target":       ft(stable+transit, "shadowV3Target"),
        "prod_chatter":          chatter(voiced, "targetMidi"),
        "v2_chatter":            chatter(voiced, "shadowV2Target"),
        "v3_chatter":            chatter(voiced, "shadowV3Target"),
        "prod_octave_errors":    oct_err(voiced, "targetMidi"),
        "v2_octave_errors":      oct_err(voiced, "shadowV2Target"),
        "v3_octave_errors":      oct_err(voiced, "shadowV3Target"),
    }

# ---------------------------------------------------------------------------
# Phase 4 — aggregate + GO gate
# ---------------------------------------------------------------------------
def aggregate_and_gate_v3(results, selected_config):
    all_v = []
    cats  = {}
    for r in results:
        frs = [f for f in r["frames"] if f.get("voiced") and f.get("gtNoteRole")]
        all_v.extend(frs)
        cats.setdefault(r["category"], []).append(r["metrics"])

    stable  = [f for f in all_v if f.get("gtNoteRole") == Role.STABLE_TARGET]
    transit = [f for f in all_v if f.get("gtNoteRole") in (Role.SLIDE, Role.SCOOP, Role.RELEASE)]
    passing = [f for f in all_v if f.get("gtNoteRole") == Role.PASSING_TONE]
    # Fall/scoop frames for overshoot check
    fall_scoop = [f for f in transit if f.get("gtNoteRole") in (Role.SCOOP, Role.RELEASE)]

    def acc(frs, key):
        if not frs: return None
        return sum(1 for f in frs if f.get(key)==f.get("gtIntendedDest"))/len(frs)
    def pres(frs, key):
        if not frs: return None
        return sum(1 for f in frs if f.get(key)!=round(f.get("gtPerformedNote") or 0))/len(frs)
    def cht(frs, key):
        if len(frs)<2: return 0.0
        return sum(1 for i in range(1,len(frs)) if frs[i].get(key)!=frs[i-1].get(key))/len(frs)
    def ft(frs, key):
        s=[f for f in frs if f.get("gtShouldCorrectNow")]
        if not s: return 0.0
        return sum(1 for f in s if f.get(key)!=f.get("gtIntendedDest"))/len(s)
    def oe(frs, key):
        return sum(1 for f in frs if abs((f.get(key) or 0)-(f.get("gtIntendedDest") or 0))>=12)

    m = {
        "total_voiced":      len(all_v),
        "prod_stable_acc":   acc(stable,  "targetMidi"),
        "v2_stable_acc":     acc(stable,  "shadowV2Target"),
        "v3_stable_acc":     acc(stable,  "shadowV3Target"),
        "prod_dest_acc":     acc(transit, "targetMidi"),
        "v2_dest_acc":       acc(transit, "shadowV2Target"),
        "v3_dest_acc":       acc(transit, "shadowV3Target"),
        "prod_pass_pres":    pres(passing,"targetMidi"),
        "v2_pass_pres":      pres(passing,"shadowV2Target"),
        "v3_pass_pres":      pres(passing,"shadowV3Target"),
        "prod_false_target": ft(all_v,    "targetMidi"),
        "v2_false_target":   ft(all_v,    "shadowV2Target"),
        "v3_false_target":   ft(all_v,    "shadowV3Target"),
        "prod_chatter":      cht(all_v,   "targetMidi"),
        "v2_chatter":        cht(all_v,   "shadowV2Target"),
        "v3_chatter":        cht(all_v,   "shadowV3Target"),
        "prod_oe":           oe(all_v,    "targetMidi"),
        "v2_oe":             oe(all_v,    "shadowV2Target"),
        "v3_oe":             oe(all_v,    "shadowV3Target"),
        "v3_dest_fall_scoop":acc(fall_scoop, "shadowV3Target"),
        "prod_dest_fall_scoop": acc(fall_scoop, "targetMidi"),
        "cats": cats,
    }

    def fmt(v): return f"{v:.1%}" if v is not None else "—"

    # Reason code distribution
    v3_reasons = {}
    for f in all_v:
        rc = f.get("shadowV3Reason","?")
        v3_reasons[rc] = v3_reasons.get(rc,0) + 1

    rep_lines = [
        "INTENT ENGINE V3 REPORT",
        f"CME config selected: {selected_config[0]} ({selected_config[1]}@{int(selected_config[2]*1000)}ms)",
        f"Voiced frames: {m['total_voiced']:,}",
        "",
        "                              prod          v2         v3",
        f"Stable-target accuracy:     {fmt(m['prod_stable_acc']):>8s}    {fmt(m['v2_stable_acc']):>8s}   {fmt(m['v3_stable_acc']):>8s}",
        f"Destination accuracy:        {fmt(m['prod_dest_acc']):>8s}    {fmt(m['v2_dest_acc']):>8s}   {fmt(m['v3_dest_acc']):>8s}",
        f"Passing-tone preservation:   {fmt(m['prod_pass_pres']):>8s}    {fmt(m['v2_pass_pres']):>8s}   {fmt(m['v3_pass_pres']):>8s}",
        f"False-target rate:           {fmt(m['prod_false_target']):>8s}    {fmt(m['v2_false_target']):>8s}   {fmt(m['v3_false_target']):>8s}",
        f"Chatter rate:                {fmt(m['prod_chatter']):>8s}    {fmt(m['v2_chatter']):>8s}   {fmt(m['v3_chatter']):>8s}",
        f"Octave errors:               {m['prod_oe']:>8}    {m['v2_oe']:>8}   {m['v3_oe']:>8}",
        f"Fall/scoop dest acc:         {fmt(m['prod_dest_fall_scoop']):>8s}    {'—':>8s}   {fmt(m['v3_dest_fall_scoop']):>8s}",
    ]
    report = "\n".join(rep_lines)

    # --- GO gate (from user spec) ---
    def _g(v): return v or 0.0

    # 1. destination accuracy materially improves
    dest_ok = _g(m["v3_dest_acc"]) > _g(m["prod_dest_acc"]) + 0.03

    # 2. false targets decrease or stay safely lower (≤ prod + 0.02 headroom)
    ft_ok = _g(m["v3_false_target"]) <= _g(m["prod_false_target"]) + 0.02

    # 3. chatter does not increase (allow 10% slack)
    chatter_ok = _g(m["v3_chatter"]) <= _g(m["prod_chatter"]) * 1.10

    # 4. passing-tone preservation does not regress (allow -2% slack)
    pass_ok = _g(m["v3_pass_pres"]) >= _g(m["prod_pass_pres"]) - 0.02

    # 5. octave errors = 0
    oct_ok = m["v3_oe"] == 0

    # 6. no systematic fall/scoop overshoot
    fs_ok = (m["v3_dest_fall_scoop"] is None
             or _g(m["v3_dest_fall_scoop"]) >= _g(m["prod_dest_fall_scoop"]) - 0.05)

    # 7. stable accuracy must not regress
    stable_ok = _g(m["v3_stable_acc"]) >= _g(m["prod_stable_acc"]) - 0.01

    go = dest_ok and ft_ok and chatter_ok and pass_ok and oct_ok and fs_ok and stable_ok

    if go:
        gate = ("GO — ACTIVATE kSlideDestinationV2Enabled = true\n"
                "All 6 criteria met. Set kSlideDestinationV2Enabled=true and run audio A/B test.")
    else:
        missing = []
        if not dest_ok:
            missing.append(f"dest accuracy not materially better "
                           f"(prod={fmt(m['prod_dest_acc'])} v3={fmt(m['v3_dest_acc'])}; need +3%)")
        if not ft_ok:
            missing.append(f"false targets too high "
                           f"(prod={fmt(m['prod_false_target'])} v3={fmt(m['v3_false_target'])})")
        if not chatter_ok:
            missing.append(f"v3 chatter exceeds prod "
                           f"(prod={fmt(m['prod_chatter'])} v3={fmt(m['v3_chatter'])})")
        if not pass_ok:
            missing.append(f"passing-tone preservation regressed "
                           f"(prod={fmt(m['prod_pass_pres'])} v3={fmt(m['v3_pass_pres'])})")
        if not oct_ok:
            missing.append(f"octave errors: {m['v3_oe']}")
        if not fs_ok:
            missing.append(f"fall/scoop overshoot "
                           f"(prod={fmt(m['prod_dest_fall_scoop'])} v3={fmt(m['v3_dest_fall_scoop'])})")
        if not stable_ok:
            missing.append(f"stable accuracy regressed "
                           f"(prod={fmt(m['prod_stable_acc'])} v3={fmt(m['v3_stable_acc'])})")
        gate = "HOLD — SHADOW MODE\nMissing:\n" + "\n".join(f"  • {x}" for x in missing)

    return gate, report, m, v3_reasons

# ---------------------------------------------------------------------------
# Report writer
# ---------------------------------------------------------------------------
def write_report_v3(results, gate, summary, agg, v3_reasons, cme_report, selected_config, scores):
    cats = {}
    for r in results:
        cats.setdefault(r["category"], []).append(r)

    all_v = []
    for r in results:
        all_v.extend([f for f in r["frames"] if f.get("voiced") and f.get("gtNoteRole")])
    n_v = len(all_v) or 1

    lines = [
        "# Intent Engine v3 Report",
        "",
        "## Phase 1 — CME Config Analysis (Real YIN Motion)",
        "",
        "| Config | Est@ms | Vel Noise σ | Flip Rate | False Slide | Vibrato Recall | Vib FP | n_stable |",
        "|--------|--------|-------------|-----------|-------------|----------------|--------|----------|",
    ]
    for tag, est, win in CME_CONFIGS:
        m = cme_report[tag]
        lines.append(
            f"| {tag} | {est}@{int(win*1000)}ms "
            f"| {m['vel_noise_std']:.4f} | {m['flip_rate']:.1%} "
            f"| {m['false_slide_rate']:.1%} | {m['vibrato_recall']:.1%} "
            f"| {m['vibrato_fp_rate']:.1%} | {m['n_stable']:,} |"
        )

    lines += [
        "",
        "## Phase 2 — Estimator Selection",
        "",
        "| Config | Score | Selected |",
        "|--------|-------|----------|",
    ]
    for tag,_,_ in CME_CONFIGS:
        sel = "✓" if tag == selected_config[0] else ""
        lines.append(f"| {tag} | {scores[tag]:+.3f} | {sel} |")

    lines += [
        "",
        f"**Winner:** {selected_config[0]} ({selected_config[1]}@{int(selected_config[2]*1000)}ms)",
        f"  - MotionBench synthetic winner: FD@20ms (1.788 MIDI/sec RMSE)",
        f"  - Combined score includes 2.0-point synthetic bonus for config A",
        "",
        "---",
        "",
        "## Phase 4 — IntentBench Results",
        "",
        "```",
        summary,
        "```",
        "",
        "---",
        "",
        "## Phase 5 — GO/HOLD Gate",
        "",
        f"```\n{gate}\n```",
        "",
        "---",
        "",
        "## Metric Dimensions by Category",
        "",
        "| Category | N | Prod Stab | v2 Stab | v3 Stab | Prod Dest | v3 Dest | v3 Pass | v3 FT | v3 Chat |",
        "|----------|---|-----------|---------|---------|-----------|---------|---------|-------|---------|",
    ]
    def fmt(v): return f"{v:.0%}" if v is not None else "—"
    for cat, rs in sorted(cats.items()):
        def cagg(key):
            vals=[r["metrics"].get(key) for r in rs if r["metrics"].get(key) is not None]
            return sum(vals)/len(vals) if vals else None
        lines.append(
            f"| {cat} | {len(rs)} "
            f"| {fmt(cagg('prod_stable_acc'))} | {fmt(cagg('v2_stable_acc'))} | {fmt(cagg('v3_stable_acc'))} "
            f"| {fmt(cagg('prod_dest_acc'))} | {fmt(cagg('v3_dest_acc'))} "
            f"| {fmt(cagg('v3_pass_pres'))} "
            f"| {fmt(cagg('v3_false_target'))} "
            f"| {fmt(cagg('v3_chatter'))} |"
        )

    lines += [
        "",
        "---",
        "",
        "## v3 Reason Code Distribution",
        "",
        "| Reason | Frames | % |",
        "|--------|--------|---|",
    ]
    for rc, cnt in sorted(v3_reasons.items(), key=lambda x: -x[1]):
        lines.append(f"| {rc} | {cnt:,} | {cnt/n_v:.1%} |")

    # Worst clusters by v3 stable accuracy
    worst = sorted(results, key=lambda r: (r["metrics"].get("v3_stable_acc") or 1.0))[:8]
    lines += [
        "",
        "---",
        "",
        "## Worst Failure Clusters (v3)",
        "",
        "| Case | Category | Prod Stab | v2 Stab | v3 Stab | v3 Dest |",
        "|------|----------|-----------|---------|---------|---------|",
    ]
    for r in worst:
        mm = r["metrics"]
        lines.append(
            f"| {r['id']} | {r['category']} "
            f"| {fmt(mm.get('prod_stable_acc'))} | {fmt(mm.get('v2_stable_acc'))} "
            f"| {fmt(mm.get('v3_stable_acc'))} | {fmt(mm.get('v3_dest_acc'))} |"
        )

    path = OUT / "INTENT_ENGINE_V3_REPORT.md"
    path.write_text("\n".join(lines))
    print(f"\nReport: {path}")

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    if not BENCH.exists():
        print(f"ERROR: bench binary not found: {BENCH}"); sys.exit(1)

    print("IntentBench v3 — Real-Motion + Shadow Validation")
    print("=" * 55)

    print("Building corpus...")
    base_cases = make_corpus()
    adv_cases  = make_adversarial(base_cases)
    all_cases  = base_cases + adv_cases
    print(f"  {len(base_cases)} base + {len(adv_cases)} adversarial = {len(all_cases)} total\n")

    # Write audio (for cases where v2 corpus WAV may not exist yet)
    for case in all_cases:
        wav_dir = "adversarial" if case["category"].startswith("adv_") else "corpus"
        wav_path = OUT / wav_dir / f"{case['id']}.wav"
        if not wav_path.exists():
            write_wav(wav_path, case["gen"]())

    print("Phase 1+4: Loading traces and running shadow analysis...")
    all_results = []

    for i, case in enumerate(all_cases):
        wav_dir = "adversarial" if case["category"].startswith("adv_") else "corpus"
        wav_path = OUT / wav_dir / f"{case['id']}.wav"
        print(f"  [{i+1:3}/{len(all_cases)}] {case['id']} ... ", end="", flush=True)

        frames = load_or_run_bench(case["id"], wav_path, case["key"], case["scale"], case["category"])
        if frames is None:
            print("SKIP (bench failed)"); continue

        frames = enrich_frames(frames)
        frames = label_frames(frames, case.get("intent", []))
        frames = enrich_with_cme(frames)   # adds cme_A_vel etc. (Phase 1)
        frames = shadow_v2(frames, case["key"], case["scale"])
        # shadow_v3 applied after Phase 2 (config selection) — placeholder here
        # We'll run shadow_v3 in Phase 3 after selecting the config

        metrics = compute_metrics_v3(frames)   # v3 fields will be 0 until shadow_v3 runs

        pa = metrics.get("prod_stable_acc"); v2a = metrics.get("v2_stable_acc")
        print(f"stab: prod={pa:.0%} v2={v2a:.0%}" if pa is not None else "ok")

        all_results.append({
            "id": case["id"],
            "category": case["category"],
            "key": case["key"],
            "scale": case["scale"],
            "frames": frames,
            "metrics": metrics,
        })

    print(f"\nPhase 1: Analyzing CME configs on {len(all_results)} cases...")
    cme_report = analyze_cme_configs(all_results)

    print("\nPhase 1 results:")
    print(f"  {'Config':6s} {'Est@ms':10s} {'VelNoise':10s} {'FalseSlide':12s} {'FlipRate':10s}")
    for tag, est, win in CME_CONFIGS:
        m = cme_report[tag]
        print(f"  {tag:6s} {est+'@'+str(int(win*1000))+'ms':10s} "
              f"{m['vel_noise_std']:10.4f} {m['false_slide_rate']:12.1%} {m['flip_rate']:10.1%}")

    print("\nPhase 2: Selecting best estimator...")
    selected = select_best_config(cme_report)
    sel_tag, sel_est, sel_win, scores = selected
    print(f"  Scores: {scores}")
    print(f"  Winner: {sel_tag} ({sel_est}@{int(sel_win*1000)}ms)")
    print(f"  Synthetic winner was FD@20ms (1.788 MIDI/sec RMSE) → config A bonus applied")

    print(f"\nPhase 3: Running shadow_v3 (SLIDE_DESTINATION_V2, {sel_est}@{int(sel_win*1000)}ms)...")
    for r in all_results:
        r["frames"] = shadow_v3(r["frames"], r["key"], r["scale"],
                                est=sel_est, window_sec=sel_win)
        r["metrics"] = compute_metrics_v3(r["frames"])

        # Save per-case v3 trace
        tr_path = OUT / "traces" / f"{r['id']}_v3.json"
        tr_path.write_text(json.dumps({
            "id": r["id"], "metrics": r["metrics"],
            "selectedConfig": f"{sel_tag}:{sel_est}@{int(sel_win*1000)}ms",
            "frames": [{k:v for k,v in f.items()} for f in r["frames"]],
        }, indent=None))

    print("\nPhase 4: Aggregating IntentBench results...")
    gate, summary, agg, v3_reasons = aggregate_and_gate_v3(all_results, selected)

    write_report_v3(all_results, gate, summary, agg, v3_reasons,
                    cme_report, selected, scores)

    print("\n" + "=" * 55)
    print(summary)
    print("\n" + gate)

    # Phase 5: Category-level adversarial summary
    print("\n\nPhase 5 — Adversarial motion analysis by category:")
    cats = {}
    for r in all_results:
        cats.setdefault(r["category"], []).append(r)
    for cat, rs in sorted(cats.items()):
        dest_v3 = [r["metrics"].get("v3_dest_acc") for r in rs if r["metrics"].get("v3_dest_acc") is not None]
        dest_prod = [r["metrics"].get("prod_dest_acc") for r in rs if r["metrics"].get("prod_dest_acc") is not None]
        ft_v3 = [r["metrics"].get("v3_false_target") for r in rs if r["metrics"].get("v3_false_target") is not None]
        def mean(lst): return sum(lst)/len(lst) if lst else None
        dv = mean(dest_v3); dp = mean(dest_prod); ft = mean(ft_v3)
        delta = f"{(dv-dp):+.0%}" if dv is not None and dp is not None else "—"
        dv_s = f"{dv:.0%}" if dv is not None else "—"
        ft_s = f"{ft:.0%}" if ft is not None else "—"
        print(f"  {cat:30s} n={len(rs):3d}  dest_v3={dv_s}  Δ={delta}  v3_ft={ft_s}")


if __name__ == "__main__":
    main()
