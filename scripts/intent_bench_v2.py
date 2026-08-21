#!/usr/bin/env python3
"""
IntentBench v2 — Intent Engine Phase 2
Baseline commit: 22a594d

Changes from v1:
- Intent-labeled ground truth per case (noteRole, shouldCorrectNow)
- Intent Engine v2: pitch direction history, candidate distance weighting,
  passing-tone suppressor, target persistence
- Reason codes per decision
- Separate metric dimensions (no single misleading accuracy number)
- Expanded corpus + denser adversarial search
"""

import json
import math
import struct
import subprocess
import sys
import wave
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

REPO   = Path(__file__).resolve().parent.parent
BENCH  = REPO / "build" / "plugins" / "auto-pitch" / "AutoPitchBench"
OUT    = REPO / "vocalbench-results" / "intent-bench-v2"
SR     = 44100
BLOCK  = 512
BLOCK_SEC = BLOCK / SR

OUT.mkdir(parents=True, exist_ok=True)
(OUT / "corpus").mkdir(exist_ok=True)
(OUT / "adversarial").mkdir(exist_ok=True)
(OUT / "traces").mkdir(exist_ok=True)

# ---------------------------------------------------------------------------
# Note roles
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

# Reason codes for shadow engine decisions
class Reason:
    NEAREST_STABLE           = "NEAREST_STABLE"
    DIRECTIONAL_DESTINATION  = "DIRECTIONAL_DESTINATION"
    PASSING_TONE             = "PASSING_TONE"
    SLIDE_DESTINATION        = "SLIDE_DESTINATION"
    STABILITY_ANCHOR         = "STABILITY_ANCHOR"
    VIBRATO_HOLD             = "VIBRATO_HOLD"
    LOW_CONFIDENCE_FALLBACK  = "LOW_CONFIDENCE_FALLBACK"

# ---------------------------------------------------------------------------
# Audio helpers
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
    n = int(dur * sr)
    out, p = [], p0
    inc = 2 * math.pi * hz / sr
    for _ in range(n):
        out.append(amp * math.sin(p)); p += inc
    return out, p % (2 * math.pi)

def slide(hz0, hz1, dur, amp=0.7, sr=SR, p0=0.0):
    n = int(dur * sr)
    out, p = [], p0
    for i in range(n):
        t = i / n
        hz = hz0 * (hz1 / hz0) ** t
        out.append(amp * math.sin(p)); p += 2 * math.pi * hz / sr
    return out, p % (2 * math.pi)

def vibrato(center_hz, depth_c, rate_hz, dur, amp=0.7, sr=SR, p0=0.0):
    n = int(dur * sr)
    out, p, lp = [], p0, 0.0
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
# Scale helpers
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
# Corpus — extended with intent labels
# ---------------------------------------------------------------------------
A3,B3,C4,D4,E4,F4,G4,A4 = 57,59,60,62,64,65,67,69

@dataclass
class IntentSegment:
    """Time-span intent annotation for a case."""
    t_start: float
    t_end: float
    performed_note: float     # actual performed MIDI (may be fractional during slides)
    intended_destination: int # ultimate intended note
    note_role: str
    should_correct_now: bool  # should production correct toward intended at this moment?

def make_corpus():
    cases = []

    # ---------- BOUNDARY ±40-60¢ ----------
    for off_c, tag in [(-40,"m40"),(-50,"m50"),(-60,"m60"),(+40,"p40"),(+50,"p50"),(+60,"p60")]:
        hz = midi_to_hz(A3 + off_c/100)
        def _gen(h=hz): s,_ = sine(h, 2.5); return env(s)
        cases.append(dict(
            id=f"BC_A3_{tag}", category="boundary",
            key="A", scale="minor",
            gen=_gen,
            intent=[
                IntentSegment(0.0, 2.5, A3 + off_c/100, A3,
                              Role.STABLE_TARGET, True),
            ]
        ))

    # ---------- SEMITONE SLIDES ----------
    for dur_ms in [100, 150, 200, 300, 400]:
        pre_dur = 0.5; post_dur = 1.0
        sl_dur  = dur_ms / 1000
        tot = pre_dur + sl_dur + post_dur
        def _gen(sd=sl_dur):
            a,p = sine(midi_to_hz(A3), pre_dur)
            b,p = slide(midi_to_hz(A3), midi_to_hz(B3), sd, p0=p)
            c,_ = sine(midi_to_hz(B3), post_dur, p0=p)
            return env(a+b+c)
        cases.append(dict(
            id=f"SLIDE_semi_{dur_ms}ms", category="semitone_slide",
            key="A", scale="minor",
            gen=_gen,
            intent=[
                IntentSegment(0.0,          pre_dur,            A3,         A3, Role.STABLE_TARGET, True),
                IntentSegment(pre_dur,       pre_dur+sl_dur,    A3+0.5,     B3, Role.SLIDE,         False),
                IntentSegment(pre_dur+sl_dur, tot,             B3,          B3, Role.STABLE_TARGET, True),
            ]
        ))

    # ---------- WHOLE-TONE SLIDES ----------
    dest_wt = B3 + 2  # C#4 — NOT in A minor; intended is D4 (in key)
    for dur_ms in [200, 400]:
        pre_dur = 0.4; post_dur = 1.0; sl_dur = dur_ms / 1000
        def _gen(sd=sl_dur):
            a,p = sine(midi_to_hz(A3), pre_dur)
            b,p = slide(midi_to_hz(A3), midi_to_hz(D4), sd, p0=p)
            c,_ = sine(midi_to_hz(D4), post_dur, p0=p)
            return env(a+b+c)
        cases.append(dict(
            id=f"SLIDE_whole_{dur_ms}ms", category="whole_tone_slide",
            key="A", scale="minor",
            gen=_gen,
            intent=[
                IntentSegment(0.0,        pre_dur,          A3,      A3, Role.STABLE_TARGET, True),
                IntentSegment(pre_dur,     pre_dur+sl_dur,  A3+2.5,  D4, Role.SLIDE,         False),
                IntentSegment(pre_dur+sl_dur, pre_dur+sl_dur+post_dur, D4, D4, Role.STABLE_TARGET, True),
            ]
        ))

    # ---------- SCOOPS ----------
    for depth_c in [30, 60, 100]:
        sl_dur = 0.15; post_dur = 1.5
        start_hz = midi_to_hz(A3 - depth_c/100)
        def _gen(sh=start_hz):
            b,p = slide(sh, midi_to_hz(A3), sl_dur)
            c,_ = sine(midi_to_hz(A3), post_dur, p0=p)
            return env(b+c, atk=0.001)
        cases.append(dict(
            id=f"SCOOP_A3_{depth_c}c", category="scoop",
            key="A", scale="minor",
            gen=_gen,
            intent=[
                IntentSegment(0.0,    sl_dur,         A3-depth_c/100, A3, Role.SCOOP,         False),
                IntentSegment(sl_dur, sl_dur+post_dur, A3,            A3, Role.STABLE_TARGET, True),
            ]
        ))

    # ---------- CHROMATIC PASSING TONE (A3→Bb3→B3) ----------
    # Bb3 is OUT of A minor but is deliberate phrasing — must NOT count as false target
    for bb_dur_ms in [60, 100, 150, 200]:
        pre=0.4; bb=bb_dur_ms/1000; post=0.8
        def _gen(bd=bb):
            a,p = sine(midi_to_hz(A3), pre)
            b,p = sine(midi_to_hz(A3+1), bd, p0=p)  # Bb3
            c,_ = sine(midi_to_hz(B3), post, p0=p)
            return env(a+b+c, atk=0.005)
        cases.append(dict(
            id=f"CHROM_pass_Bb3_{bb_dur_ms}ms", category="chromatic_passing",
            key="A", scale="minor",
            gen=_gen,
            intent=[
                IntentSegment(0.0,      pre,      A3,   B3, Role.STABLE_TARGET, True),
                IntentSegment(pre,      pre+bb,   A3+1, B3, Role.PASSING_TONE,  False),  # Bb NOT to be corrected hard
                IntentSegment(pre+bb,   pre+bb+post, B3, B3, Role.STABLE_TARGET, True),
            ]
        ))

    # ---------- INTENTIONAL OUT-OF-SCALE (deliberate blues note) ----------
    # Singer holds a b7 (G3 in A minor context = in scale) but then bends to Ab3
    # (outside key) as an expressive choice before returning — should preserve
    for depth_c in [50, 80]:
        pre=0.6; bend=0.25; post=0.8
        def _gen(dc=depth_c):
            a,p = sine(midi_to_hz(G4-12), pre)
            b,p = slide(midi_to_hz(G4-12), midi_to_hz(G4-12+dc/100), bend, p0=p)
            c,_ = sine(midi_to_hz(G4-12), post, p0=p)
            return env(a+b+c)
        cases.append(dict(
            id=f"OOS_blues_G3_bend{depth_c}c", category="out_of_scale",
            key="A", scale="minor",
            gen=_gen,
            intent=[
                IntentSegment(0.0, pre,        G4-12,           G4-12, Role.STABLE_TARGET, True),
                IntentSegment(pre, pre+bend,    G4-12+depth_c/100, G4-12, Role.VIBRATO_EXCURSION, False),
                IntentSegment(pre+bend, pre+bend+post, G4-12, G4-12, Role.STABLE_TARGET, True),
            ]
        ))

    # ---------- WRONG-NOTE RECOVERY ----------
    # Singer accidentally hits the wrong note briefly, then corrects
    for wrong_dur_ms in [80, 150]:
        wd = wrong_dur_ms/1000
        def _gen(d=wd):
            a,p = sine(midi_to_hz(A3), 0.5)
            b,p = sine(midi_to_hz(A3+2), d, p0=p)  # C#3 — wrong note
            c,_ = sine(midi_to_hz(A3), 0.8, p0=p)
            return env(a+b+c, atk=0.005, rel=0.01)
        cases.append(dict(
            id=f"WRONG_note_{wrong_dur_ms}ms", category="wrong_note",
            key="A", scale="minor",
            gen=_gen,
            intent=[
                IntentSegment(0.0, 0.5,       A3,    A3, Role.STABLE_TARGET, True),
                IntentSegment(0.5, 0.5+wd,    A3+2,  A3, Role.WRONG_NOTE,   True),   # should correct
                IntentSegment(0.5+wd, 1.3+wd, A3,    A3, Role.STABLE_TARGET, True),
            ]
        ))

    # ---------- VIBRATO BOUNDARY CROSSINGS ----------
    for depth_c in [30, 50, 70]:
        center = A3 + 0.5
        def _gen(d=depth_c, c=center):
            s,_ = vibrato(midi_to_hz(c), d, 5.0, 3.0)
            return env(s)
        cases.append(dict(
            id=f"VIB_cross_{depth_c}c", category="vibrato_boundary",
            key="A", scale="minor",
            gen=_gen,
            intent=[
                IntentSegment(0.0, 3.0, center, A3, Role.VIBRATO_EXCURSION, False),
            ]
        ))

    # ---------- RUNS ----------
    run_notes = [A3, B3, C4, D4, E4]
    for nd_ms in [80, 120, 200, 350]:
        def _gen(nd=nd_ms/1000):
            out=[]; p=0.0
            for note in run_notes:
                s,p = sine(midi_to_hz(note), nd, p0=p)
                out += env(s, atk=0.005, rel=0.005)
            return out
        # Each note in the run is a passing tone except the last
        intents = []
        t = 0.0
        for idx, note in enumerate(run_notes):
            role = Role.STABLE_TARGET if idx == len(run_notes)-1 else Role.PASSING_TONE
            correct = idx == len(run_notes)-1
            intents.append(IntentSegment(t, t+nd_ms/1000, note, E4, role, correct))
            t += nd_ms/1000
        cases.append(dict(
            id=f"RUN_up_{nd_ms}ms", category="run",
            key="A", scale="minor",
            gen=_gen,
            intent=intents
        ))

    # ---------- REPEATED NOTES ----------
    for n_reps in [4, 8]:
        def _gen(nr=n_reps):
            out=[]; p=0.0
            for _ in range(nr):
                s,p = sine(midi_to_hz(A3), 0.15, p0=p)
                out += env(s, atk=0.008, rel=0.008)
                out += silence(0.04)
            return out
        cases.append(dict(
            id=f"REPEAT_A3_{n_reps}x", category="repeated_note",
            key="A", scale="minor",
            gen=_gen,
            intent=[IntentSegment(0.0, 9.9, A3, A3, Role.STABLE_TARGET, True)]
        ))

    # ---------- SHORT NOTES ----------
    for dur_ms in [80, 100, 150, 200, 350, 500]:
        def _gen(d=dur_ms/1000):
            s,_ = sine(midi_to_hz(A3), d)
            return env(s, atk=0.01, rel=0.02)
        cases.append(dict(
            id=f"SHORT_A3_{dur_ms}ms", category="short_note",
            key="A", scale="minor",
            gen=_gen,
            intent=[IntentSegment(0.0, dur_ms/1000, A3, A3, Role.STABLE_TARGET, True)]
        ))

    # ---------- FALLS/RELEASES ----------
    for depth_c in [50, 100]:
        def _gen(dc=depth_c):
            a,p = sine(midi_to_hz(A3), 1.0)
            b,_ = slide(midi_to_hz(A3), midi_to_hz(A3-dc/100), 0.3, amp=0.5, p0=p)
            return env(a+b, rel=0.05)
        cases.append(dict(
            id=f"FALL_A3_{depth_c}c", category="fall",
            key="A", scale="minor",
            gen=_gen,
            intent=[
                IntentSegment(0.0, 1.0, A3,           A3, Role.STABLE_TARGET, True),
                IntentSegment(1.0, 1.3, A3-depth_c/100, A3, Role.RELEASE,    False),
            ]
        ))

    return cases


def make_adversarial(cases):
    adv = []
    seen = set()

    for case in cases:
        cat = case["category"]
        key, scale = case["key"], case["scale"]

        if cat == "boundary":
            for off in [-58,-55,-52,-48,-45,+45,+48,+52,+55,+58]:
                uid = f"ADV_BC_{off:+d}c"
                if uid in seen: continue
                seen.add(uid)
                hz = midi_to_hz(A3 + off/100)
                def _gen(h=hz): s,_ = sine(h, 2.0); return env(s)
                adv.append(dict(
                    id=uid, category="adv_boundary", key=key, scale=scale, gen=_gen,
                    intent=[IntentSegment(0,2.0, A3+off/100, A3, Role.STABLE_TARGET, True)]
                ))

        elif cat == "vibrato_boundary":
            for dep in [15, 20, 25, 35, 45, 55, 65, 80]:
                uid = f"ADV_VIB_{dep}c"
                if uid in seen: continue
                seen.add(uid)
                def _gen(d=dep): s,_ = vibrato(midi_to_hz(A3+0.5), d, 5.0, 3.0); return env(s)
                adv.append(dict(
                    id=uid, category="adv_vib_boundary", key=key, scale=scale, gen=_gen,
                    intent=[IntentSegment(0,3.0, A3+0.5, A3, Role.VIBRATO_EXCURSION, False)]
                ))

        elif cat == "semitone_slide":
            for d_ms in [50,75,100,150,200,300,500,750]:
                for start_off in [-20, 0, +20]:
                    uid = f"ADV_SLIDE_semi_{d_ms}ms_off{start_off:+d}c"
                    if uid in seen: continue
                    seen.add(uid)
                    sd = d_ms/1000
                    def _gen(s=sd, so=start_off):
                        a,p = sine(midi_to_hz(A3+so/100), 0.4)
                        b,p = slide(midi_to_hz(A3+so/100), midi_to_hz(B3), s, p0=p)
                        c,_ = sine(midi_to_hz(B3), 0.8, p0=p)
                        return env(a+b+c)
                    adv.append(dict(
                        id=uid, category="adv_slide", key=key, scale=scale, gen=_gen,
                        intent=[
                            IntentSegment(0, 0.4,       A3+start_off/100, B3, Role.STABLE_TARGET, True),
                            IntentSegment(0.4, 0.4+sd,  A3+0.5,           B3, Role.SLIDE,         False),
                            IntentSegment(0.4+sd, 9.9,  B3,               B3, Role.STABLE_TARGET, True),
                        ]
                    ))

        elif cat == "chromatic_passing":
            for bb_ms in [40, 60, 80, 100, 120, 160, 200]:
                uid = f"ADV_CHROM_{bb_ms}ms"
                if uid in seen: continue
                seen.add(uid)
                bd = bb_ms/1000
                def _gen(d=bd):
                    a,p = sine(midi_to_hz(A3), 0.35)
                    b,p = sine(midi_to_hz(A3+1), d, p0=p)
                    c,_ = sine(midi_to_hz(B3), 0.7, p0=p)
                    return env(a+b+c)
                adv.append(dict(
                    id=uid, category="adv_chromatic_pass", key=key, scale=scale, gen=_gen,
                    intent=[
                        IntentSegment(0, 0.35,     A3,   B3, Role.STABLE_TARGET, True),
                        IntentSegment(0.35, 0.35+bd, A3+1, B3, Role.PASSING_TONE, False),
                        IntentSegment(0.35+bd, 9.9, B3,   B3, Role.STABLE_TARGET, True),
                    ]
                ))

    return adv


# ---------------------------------------------------------------------------
# Bench runner
# ---------------------------------------------------------------------------
def run_bench(wav_path, key, scale):
    tr = OUT / "traces" / (wav_path.stem + "_trace.json")
    wo = OUT / "traces" / (wav_path.stem + "_out.wav")
    r = subprocess.run([
        str(BENCH), str(wav_path),
        "--output", str(wo), "--trace", str(tr),
        "--key", key, "--scale", scale,
        "--amount", "0.82", "--speed", "0.70", "--block", str(BLOCK),
    ], capture_output=True, text=True)
    if r.returncode != 0 or not tr.exists():
        return None
    return json.load(open(tr)).get("frames", [])


# ---------------------------------------------------------------------------
# Ground-truth labeling
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
        fr["gtPerformedNote"]       = seg.performed_note if seg else None
        fr["gtIntendedDest"]        = seg.intended_destination if seg else None
        fr["gtNoteRole"]            = seg.note_role if seg else None
        fr["gtShouldCorrectNow"]    = seg.should_correct_now if seg else False
    return frames


# ---------------------------------------------------------------------------
# Phase 1 baseline enrich
# ---------------------------------------------------------------------------
def enrich_frames(frames):
    prev_tgt = None
    stab = 0
    for i, fr in enumerate(frames):
        det = fr.get("detectedMidi", -1) or -1
        tgt = det   # production uses detectedMidi as the correction target
        # Compute pitch velocity
        if i > 0 and frames[i-1].get("voiced") and fr.get("voiced"):
            pv = abs(det - (frames[i-1].get("detectedMidi") or det)) / BLOCK_SEC
        else:
            pv = 0.0
        fr["pitchVelocity"] = round(pv, 3)
        # Stability
        if tgt != prev_tgt:
            stab = 0; prev_tgt = tgt
        else:
            stab += 1
        fr["targetMidi"]          = det
        fr["targetStabFrames"]    = stab
        fr["targetStabSec"]       = round(stab * BLOCK_SEC, 4)
    return frames


# ---------------------------------------------------------------------------
# Intent Engine v2
# ---------------------------------------------------------------------------
HIST_LEN   = 12   # frames of pitch history (~280ms at 512/44100)
SLIDE_VEL  = 0.6  # MIDI/sec — pitch moving this fast = sliding
PASS_DWELL = 0.18 # max seconds spent near a note to call it passing tone
STAB_SEC   = 0.10 # min seconds stable before anchoring
PERSIST_SEC= 0.12 # min seconds before changing a stable target

@dataclass
class IntentV2State:
    pitch_hist:    deque = field(default_factory=lambda: deque(maxlen=HIST_LEN))
    vel_hist:      deque = field(default_factory=lambda: deque(maxlen=HIST_LEN))
    stable_target: int   = -1
    stable_sec:    float = 0.0
    last_voiced:   float = -1.0
    dwell:         dict  = field(default_factory=dict)  # midi→frames near it
    prod_target:   int   = -1

    def smoothed_velocity(self):
        vals = list(self.vel_hist)
        if not vals: return 0.0
        # signed direction: positive = rising, negative = falling
        pitches = list(self.pitch_hist)
        if len(pitches) < 2: return 0.0
        return (pitches[-1] - pitches[0]) / (len(pitches) * BLOCK_SEC)

    def is_directionally_consistent(self, frames=6):
        pvs = list(self.vel_hist)[-frames:]
        if len(pvs) < 3: return False, 0.0
        signs = [1 if v > 0.3 else (-1 if v < -0.3 else 0) for v in pvs]
        pos = signs.count(1); neg = signs.count(-1)
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
            state.dwell.clear(); state.stable_sec = 0.0
            continue

        det  = float(fr.get("detectedMidi", -1) or -1)
        conf = float(fr.get("confidence", 0.0) or 0.0)
        pv   = float(fr.get("pitchVelocity", 0.0) or 0.0)
        prod = fr.get("targetMidi", int(round(det)))
        state.prod_target = prod

        # signed velocity
        if state.last_voiced >= 0:
            sv = (det - state.last_voiced) / BLOCK_SEC
        else:
            sv = 0.0
        state.last_voiced = det
        state.pitch_hist.append(det)
        state.vel_hist.append(sv)

        smoothed_vel = state.smoothed_velocity()
        consistent, direction = state.is_directionally_consistent()

        # --- Dwell tracking per candidate note ---
        near_note = nearest_scale(det, key, scale)
        state.dwell[near_note] = state.dwell.get(near_note, 0) + 1

        # --- Decision tree ---
        target   = prod
        r_conf   = conf * 0.6
        reason   = Reason.LOW_CONFIDENCE_FALLBACK

        # 1. Actively sliding with directional consistency → predict destination
        if abs(smoothed_vel) >= SLIDE_VEL and consistent:
            lookahead_midi = det + direction * abs(smoothed_vel) * 0.20
            destination = nearest_scale(lookahead_midi, key, scale)
            # Extra: if destination == current nearest, extend lookahead
            if destination == near_note and abs(smoothed_vel) > 1.0:
                lookahead_midi = det + direction * abs(smoothed_vel) * 0.35
                destination = nearest_scale(lookahead_midi, key, scale)
            target = destination
            r_conf = min(0.88, conf * 0.95)
            reason = Reason.SLIDE_DESTINATION

        # 2. Passing-tone suppressor: fast movement + short dwell + consistent direction
        elif (consistent
              and state.dwell.get(near_note, 0) * BLOCK_SEC < PASS_DWELL
              and abs(smoothed_vel) > 0.35
              and state.stable_target != -1):
            # Don't commit to this note — preserve the last stable target
            target = state.stable_target
            r_conf = 0.72
            reason = Reason.PASSING_TONE

        # 3. Stable anchor — pitch settled, confident
        elif (state.stable_sec >= STAB_SEC and conf >= 0.55
              and abs(smoothed_vel) < 0.5):
            candidate = nearest_scale(det, key, scale)
            # Target persistence: resist switching from stable target unless
            # pitch has moved decisively
            if (state.stable_target != -1
                    and state.stable_target != candidate
                    and abs(smoothed_vel) < 1.2
                    and state.stable_sec < PERSIST_SEC * 3):
                target = state.stable_target
                r_conf = 0.80
                reason = Reason.VIBRATO_HOLD
            else:
                target = candidate
                state.stable_target = candidate
                r_conf = min(0.95, conf)
                reason = Reason.STABILITY_ANCHOR

        # 4. Low velocity, stable target known → preserve it
        elif state.stable_target != -1 and abs(smoothed_vel) < 0.8:
            target = state.stable_target
            r_conf = min(0.78, conf * 1.1)
            reason = Reason.VIBRATO_HOLD

        # 5. Nearest scale note (fallback)
        else:
            target = nearest_scale(det, key, scale)
            r_conf = conf * 0.70
            if r_conf >= 0.50:
                reason = Reason.NEAREST_STABLE
            else:
                # Low confidence — fall back to production
                target = prod
                r_conf = conf * 0.50
                reason = Reason.LOW_CONFIDENCE_FALLBACK

        # Update stability counter
        if target == state.stable_target and abs(smoothed_vel) < 0.8:
            state.stable_sec += BLOCK_SEC
        else:
            if reason not in (Reason.PASSING_TONE, Reason.VIBRATO_HOLD):
                state.stable_sec = 0.0

        fr.update(
            shadowV2Target     = target,
            shadowV2Confidence = round(r_conf, 3),
            shadowV2Reason     = reason,
        )

    return frames


# ---------------------------------------------------------------------------
# Phase 5 — separated metric dimensions
# ---------------------------------------------------------------------------
def compute_metrics(frames, v1_key="shadowCandidate"):
    """Compute separated metrics using GT labels. Returns a dict of metric dims."""
    voiced = [f for f in frames if f.get("voiced") and f.get("gtNoteRole")]
    if not voiced:
        return {}

    def _filt(role_list):
        return [f for f in voiced if f.get("gtNoteRole") in role_list]

    stable  = _filt([Role.STABLE_TARGET])
    transit = _filt([Role.SLIDE, Role.SCOOP, Role.RELEASE])
    passing = _filt([Role.PASSING_TONE, Role.VIBRATO_EXCURSION])
    wrong   = _filt([Role.WRONG_NOTE])

    def acc(frs, key, gt_key="gtIntendedDest"):
        if not frs: return None
        return round(sum(1 for f in frs if f.get(key) == f.get(gt_key)) / len(frs), 4)

    def premature_rate(frs):
        if not frs: return None
        return round(sum(1 for f in frs
                         if f.get("shadowV2Target") == f.get("gtIntendedDest")
                         and f.get("gtShouldCorrectNow") == False
                         and f.get("gtNoteRole") == Role.SLIDE) / len(frs), 4)

    def false_target_rate(frs, key):
        if not frs: return None
        return round(sum(1 for f in frs
                         if f.get(key) != f.get("gtIntendedDest")
                         and f.get("gtShouldCorrectNow") == True) / len(frs), 4)

    def chatter(frs, key, window=5):
        if len(frs) < window: return 0.0
        switches = 0
        for i in range(1, len(frs)):
            if frs[i].get(key) != frs[i-1].get(key):
                switches += 1
        return round(switches / len(frs), 4)

    def octave_errors(frs, key):
        return sum(1 for f in frs
                   if abs((f.get(key) or 0) - (f.get("gtIntendedDest") or 0)) >= 12)

    # Passing-tone preservation: % of passing-tone frames where v2 correctly
    # does NOT commit to the passing note (target != passing note MIDI)
    def pass_preservation(frs):
        if not frs: return None
        preserved = sum(1 for f in frs
                        if f.get("shadowV2Target") != round(f.get("gtPerformedNote") or 0))
        return round(preserved / len(frs), 4)

    return {
        "n_stable":              len(stable),
        "n_transition":          len(transit),
        "n_passing":             len(passing),
        "n_wrong":               len(wrong),
        "prod_stable_acc":       acc(stable,  "targetMidi"),
        "v2_stable_acc":         acc(stable,  "shadowV2Target"),
        "prod_dest_acc":         acc(transit, "targetMidi"),
        "v2_dest_acc":           acc(transit, "shadowV2Target"),
        "prod_passing_preserve": pass_preservation(
            [f for f in passing if f.get("gtNoteRole") == Role.PASSING_TONE]),
        "v2_passing_preserve":   pass_preservation(
            [f for f in voiced if f.get("gtNoteRole") == Role.PASSING_TONE]),
        "prod_false_target":     false_target_rate(stable+transit, "targetMidi"),
        "v2_false_target":       false_target_rate(stable+transit, "shadowV2Target"),
        "v2_premature_rate":     premature_rate(transit),
        "prod_chatter":          chatter(voiced, "targetMidi"),
        "v2_chatter":            chatter(voiced, "shadowV2Target"),
        "prod_octave_errors":    octave_errors(voiced, "targetMidi"),
        "v2_octave_errors":      octave_errors(voiced, "shadowV2Target"),
    }


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    if not BENCH.exists():
        print(f"ERROR: bench binary not found: {BENCH}"); sys.exit(1)

    print("IntentBench v2 — Intent Engine Phase 2")
    print("=" * 52)
    print(f"Baseline: 22a594d | Production: 6c022cb\n")

    print("Building corpus...")
    base_cases = make_corpus()
    print(f"  {len(base_cases)} base cases")

    print("Building adversarial cases...")
    adv_cases = make_adversarial(base_cases)
    print(f"  {len(adv_cases)} adversarial cases")

    all_cases = base_cases + adv_cases
    print(f"  {len(all_cases)} total\n")

    # Write audio
    for case in all_cases:
        wav_dir = "adversarial" if case["category"].startswith("adv_") else "corpus"
        wav_path = OUT / wav_dir / f"{case['id']}.wav"
        write_wav(wav_path, case["gen"]())

    print(f"Running bench + v2 analysis...")
    all_results = []

    for i, case in enumerate(all_cases):
        wav_dir = "adversarial" if case["category"].startswith("adv_") else "corpus"
        wav_path = OUT / wav_dir / f"{case['id']}.wav"
        print(f"  [{i+1}/{len(all_cases)}] {case['id']} ... ", end="", flush=True)

        frames = run_bench(wav_path, case["key"], case["scale"])
        if frames is None:
            print("SKIP"); continue

        frames = enrich_frames(frames)
        frames = label_frames(frames, case.get("intent", []))
        frames = shadow_v2(frames, case["key"], case["scale"])
        metrics = compute_metrics(frames)

        # Quick summary
        pa = metrics.get("prod_stable_acc")
        v2 = metrics.get("v2_stable_acc")
        pp = metrics.get("v2_passing_preserve")
        pa_s = f"{pa:.0%}" if pa is not None else "—"
        v2_s = f"{v2:.0%}" if v2 is not None else "—"
        pp_s = f"pass={pp:.0%}" if pp is not None else ""
        delta = (v2 or 0) - (pa or 0)
        print(f"stab: prod={pa_s} v2={v2_s} ({delta:+.0%}) {pp_s}")

        all_results.append({
            "id": case["id"],
            "category": case["category"],
            "frames": frames,
            "metrics": metrics,
        })
        with open(OUT / "traces" / f"{case['id']}_v2.json", "w") as fh:
            json.dump({"id":case["id"], "metrics":metrics,
                       "frames":[{k:v for k,v in f.items()
                                  if k not in ("recentTargets",)} for f in frames]}, fh)

    # Aggregate
    gate, report = aggregate_and_gate(all_results)
    write_report(all_results, gate, report)

    print("\n" + "="*52)
    print(report)
    print("\n" + gate)


def aggregate_and_gate(results):
    all_v = []
    cats  = {}
    for r in results:
        frs = [f for f in r["frames"] if f.get("voiced") and f.get("gtNoteRole")]
        all_v.extend(frs)
        c = r["category"]
        cats.setdefault(c, []).append(r["metrics"])

    n = len(all_v) or 1
    stable  = [f for f in all_v if f.get("gtNoteRole") == Role.STABLE_TARGET]
    transit = [f for f in all_v if f.get("gtNoteRole") in (Role.SLIDE, Role.SCOOP, Role.RELEASE)]
    passing = [f for f in all_v if f.get("gtNoteRole") in (Role.PASSING_TONE, Role.VIBRATO_EXCURSION)]

    def acc(frs, key):
        if not frs: return None
        return sum(1 for f in frs if f.get(key) == f.get("gtIntendedDest")) / len(frs)

    def pres(frs, key):
        if not frs: return None
        return sum(1 for f in frs
                   if f.get(key) != round(f.get("gtPerformedNote") or 0)) / len(frs)

    def cht(frs, key):
        if len(frs) < 2: return 0.0
        return sum(1 for i in range(1,len(frs)) if frs[i].get(key) != frs[i-1].get(key)) / len(frs)

    def ft(frs, key):
        should = [f for f in frs if f.get("gtShouldCorrectNow")]
        if not should: return 0.0
        return sum(1 for f in should if f.get(key) != f.get("gtIntendedDest")) / len(should)

    octs_v2  = sum(1 for f in all_v if abs((f.get("shadowV2Target") or 0)-(f.get("gtIntendedDest") or 0))>=12)
    octs_prod= sum(1 for f in all_v if abs((f.get("targetMidi") or 0)-(f.get("gtIntendedDest") or 0))>=12)

    m = {
        "total_voiced":         len(all_v),
        "prod_stable_acc":      acc(stable,  "targetMidi"),
        "v2_stable_acc":        acc(stable,  "shadowV2Target"),
        "prod_dest_acc":        acc(transit, "targetMidi"),
        "v2_dest_acc":          acc(transit, "shadowV2Target"),
        "prod_pass_pres":       pres(passing, "targetMidi"),
        "v2_pass_pres":         pres(passing, "shadowV2Target"),
        "prod_false_target":    ft(all_v, "targetMidi"),
        "v2_false_target":      ft(all_v, "shadowV2Target"),
        "prod_chatter":         cht(all_v, "targetMidi"),
        "v2_chatter":           cht(all_v, "shadowV2Target"),
        "prod_octave_errors":   octs_prod,
        "v2_octave_errors":     octs_v2,
        "cats":                 cats,
    }

    def fmt(v): return f"{v:.1%}" if v is not None else "—"

    rep = "\n".join([
        "INTENT ENGINE V2 REPORT",
        f"Voiced frames: {m['total_voiced']:,}",
        "",
        "Stable-target accuracy:      prod={} v2={}".format(fmt(m["prod_stable_acc"]),  fmt(m["v2_stable_acc"])),
        "Destination accuracy:        prod={} v2={}".format(fmt(m["prod_dest_acc"]),    fmt(m["v2_dest_acc"])),
        "Passing-tone preservation:   prod={} v2={}".format(fmt(m["prod_pass_pres"]),   fmt(m["v2_pass_pres"])),
        "False-target rate:           prod={} v2={}".format(fmt(m["prod_false_target"]),fmt(m["v2_false_target"])),
        "Chatter rate:                prod={} v2={}".format(fmt(m["prod_chatter"]),     fmt(m["v2_chatter"])),
        f"Octave errors:               prod={m['prod_octave_errors']} v2={m['v2_octave_errors']}",
    ])

    # GO gate criteria
    stable_beat  = (m["v2_stable_acc"] or 0) >= (m["prod_stable_acc"] or 0)
    dest_beat    = (m["v2_dest_acc"]   or 0) >= (m["prod_dest_acc"]   or 0)
    pass_beat    = (m["v2_pass_pres"]  or 0) >  (m["prod_pass_pres"]  or 0.0) + 0.05
    ft_beat      = (m["v2_false_target"] or 0) < (m["prod_false_target"] or 1.0) - 0.01
    chatter_ok   = (m["v2_chatter"] or 0) <= (m["prod_chatter"] or 0) * 1.15
    octave_ok    = m["v2_octave_errors"] == 0
    meaningful   = pass_beat or ft_beat or (dest_beat and (m["v2_dest_acc"] or 0) - (m["prod_dest_acc"] or 0) > 0.05)

    if meaningful and stable_beat and chatter_ok and octave_ok:
        gate = "GO — ACTIVE AUDIO EXPERIMENT"
    else:
        missing = []
        if not stable_beat: missing.append("stable-target accuracy regressed")
        if not pass_beat:   missing.append(f"passing-tone preservation not materially better (prod={fmt(m['prod_pass_pres'])} v2={fmt(m['v2_pass_pres'])})")
        if not ft_beat:     missing.append(f"false-target rate not materially reduced (prod={fmt(m['prod_false_target'])} v2={fmt(m['v2_false_target'])})")
        if not chatter_ok:  missing.append("v2 chatter exceeds production")
        if not octave_ok:   missing.append(f"v2 octave errors: {m['v2_octave_errors']}")
        if not meaningful:  missing.append("improvement not meaningful enough")
        gate = "HOLD — SHADOW MODE\nMissing: " + "; ".join(missing)

    return gate, rep


def write_report(results, gate, summary):
    cats = {}
    for r in results:
        c = r["category"]
        cats.setdefault(c, []).append(r)

    all_v = []
    for r in results:
        all_v.extend([f for f in r["frames"] if f.get("voiced") and f.get("gtNoteRole")])

    # Reason code distribution
    reason_counts = {}
    for f in all_v:
        rc = f.get("shadowV2Reason", "?")
        reason_counts[rc] = reason_counts.get(rc, 0) + 1
    n_v = len(all_v) or 1

    # Worst clusters
    worst = sorted(results,
        key=lambda r: (r["metrics"].get("v2_stable_acc") or 1.0))[:8]

    lines = [
        "# Intent Engine v2 Report",
        "",
        f"**Baseline commit:** `22a594d` | **Production commit:** `6c022cb`",
        f"**Corpus:** {len(results)} cases analyzed",
        "",
        "---",
        "",
        "## Summary",
        "```",
        summary,
        "```",
        "",
        "---",
        "",
        "## Phase 6 — GO/HOLD Gate",
        "",
        f"```\n{gate}\n```",
        "",
        "---",
        "",
        "## Phase 5 — Metric Dimensions by Category",
        "",
        "| Category | N | Prod Stab | v2 Stab | Prod Dest | v2 Dest | v2 Pass Pres | v2 FT | v2 Chat |",
        "|----------|---|-----------|---------|-----------|---------|--------------|-------|---------|",
    ]
    def fmt(v): return f"{v:.0%}" if v is not None else "—"
    for cat, rs in sorted(cats.items()):
        def cat_agg(key):
            vals = [r["metrics"].get(key) for r in rs if r["metrics"].get(key) is not None]
            return sum(vals)/len(vals) if vals else None
        lines.append(
            f"| {cat} | {len(rs)} "
            f"| {fmt(cat_agg('prod_stable_acc'))} | {fmt(cat_agg('v2_stable_acc'))} "
            f"| {fmt(cat_agg('prod_dest_acc'))} | {fmt(cat_agg('v2_dest_acc'))} "
            f"| {fmt(cat_agg('v2_passing_preserve'))} "
            f"| {fmt(cat_agg('v2_false_target'))} "
            f"| {fmt(cat_agg('v2_chatter'))} |"
        )

    lines += [
        "",
        "---",
        "",
        "## Phase 3 — Shadow v2 Reason Code Distribution",
        "",
        "| Reason | Frames | % |",
        "|--------|--------|---|",
    ]
    for rc, cnt in sorted(reason_counts.items(), key=lambda x: -x[1]):
        lines.append(f"| {rc} | {cnt:,} | {cnt/n_v:.1%} |")

    lines += [
        "",
        "---",
        "",
        "## Worst Failure Clusters",
        "",
        "| Case | Category | Prod Stab | v2 Stab | v2 Pass Pres |",
        "|------|----------|-----------|---------|--------------|",
    ]
    for r in worst:
        m = r["metrics"]
        lines.append(
            f"| {r['id']} | {r['category']} "
            f"| {fmt(m.get('prod_stable_acc'))} | {fmt(m.get('v2_stable_acc'))} "
            f"| {fmt(m.get('v2_passing_preserve'))} |"
        )

    lines += [
        "",
        "---",
        "",
        "## v2 Architecture — Phase 3",
        "",
        "Input features: detectedMidi, smoothedVelocity (12-frame history),",
        "directionConsistency, dwellFrames per candidate, prevStableTarget,",
        "stabilityDuration, confidence, key/scale.",
        "",
        "Decision tiers:",
        "1. **SLIDE_DESTINATION**: abs(smoothedVel) ≥ 0.6 MIDI/sec + consistent →",
        "   extrapolate 200-350ms ahead, snap to nearest scale note.",
        "2. **PASSING_TONE**: consistent direction + dwell < 180ms + stable target known →",
        "   hold previous stable target, do not commit to passing note.",
        "3. **STABILITY_ANCHOR**: stable ≥ 100ms + conf ≥ 0.55 + low velocity →",
        "   lock to nearest scale note with high confidence.",
        "4. **VIBRATO_HOLD**: stable target known + low velocity → preserve it.",
        "5. **LOW_CONFIDENCE_FALLBACK**: fall through to production target.",
        "",
        "Strategy G correction dynamics: **FROZEN**.",
    ]

    path = OUT / "INTENT_ENGINE_V2_REPORT.md"
    path.write_text("\n".join(lines))
    print(f"\nReport: {path}")


if __name__ == "__main__":
    main()
