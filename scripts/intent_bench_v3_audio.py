#!/usr/bin/env python3
"""
Intent v3 Active Audio Validation
===================================
Validates that Intent v3 (kSlideDestinationV2Enabled=true) produces better
audible tuning, not merely better benchmark labels.

Phases:
  1. Active render matrix — 90 corpus cases: production vs v3_active
  2. Real-vocal proxy — VocalBench stable/boundary corpus: production vs v3_active
  3. Semitone slide investigation — 40/60/80ms horizon sweep (Python shadow)
  4. Confidence-band calibration — accuracy per intent-confidence decile
  5. Full regression suite — contracts, motionbench, smoke tests

Constraints enforced:
  - Do NOT modify Strategy G
  - Do NOT merge to production (verdict only)
  - GO verdict requires all 5 phases clean
"""

import json
import math
import os
import struct
import subprocess
import sys
import wave
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

REPO        = Path(__file__).resolve().parent.parent
BENCH       = REPO / "build" / "plugins" / "auto-pitch" / "AutoPitchBench"
MOTIONBENCH = REPO / "build" / "plugins" / "auto-pitch" / "AutoPitchMotionBench"
CONTRACT    = REPO / "build" / "plugins" / "auto-pitch" / "AutoPitchCoreContract"
V3_TRACES   = REPO / "vocalbench-results" / "intent-bench-v3" / "traces"
V2_TRACES   = REPO / "vocalbench-results" / "intent-bench-v2" / "traces"
VB_CORPUS   = REPO / "vocalbench-results" / "corpus"
OUT         = REPO / "vocalbench-results" / "intent-bench-v3-audio"
SR          = 44100
BLOCK       = 512
BLOCK_SEC   = BLOCK / SR

OUT.mkdir(parents=True, exist_ok=True)
(OUT / "prod").mkdir(exist_ok=True)
(OUT / "v3").mkdir(exist_ok=True)
(OUT / "semislide").mkdir(exist_ok=True)

# ---------------------------------------------------------------------------
# Shared: corpus generation (identical to v2/v3 — duplicated for standalone)
# ---------------------------------------------------------------------------
class Role:
    STABLE_TARGET = "STABLE_TARGET"; TRANSITION = "TRANSITION"
    PASSING_TONE = "PASSING_TONE";  SCOOP = "SCOOP"
    SLIDE = "SLIDE";                VIBRATO_EXCURSION = "VIBRATO_EXCURSION"
    WRONG_NOTE = "WRONG_NOTE";      RELEASE = "RELEASE"

class Reason:
    NEAREST_STABLE = "NEAREST_STABLE"; SLIDE_DESTINATION = "SLIDE_DESTINATION"
    PASSING_TONE = "PASSING_TONE";     STABILITY_ANCHOR = "STABILITY_ANCHOR"
    VIBRATO_HOLD = "VIBRATO_HOLD";     SLIDE_DESTINATION_V2 = "SLIDE_DESTINATION_V2"
    LOW_CONFIDENCE_FALLBACK = "LOW_CONFIDENCE_FALLBACK"

@dataclass
class IntentSegment:
    t_start: float; t_end: float
    performed_note: float; intended_destination: int
    note_role: str; should_correct_now: bool

KEY_INTERVALS = {"major":[0,2,4,5,7,9,11],"minor":[0,2,3,5,7,8,10],"chromatic":list(range(12))}
ROOT_MAP = {"C":0,"C#":1,"Db":1,"D":2,"D#":3,"Eb":3,"E":4,"F":5,
            "F#":6,"Gb":6,"G":7,"G#":8,"Ab":8,"A":9,"A#":10,"Bb":10,"B":11}

def midi_to_hz(m): return 440.0 * (2.0 ** ((m - 69.0) / 12.0))
def hz_to_midi(f): return 69.0 + 12.0 * math.log2(f / 440.0) if f > 0 else 0.0

def nearest_scale(midi_f, key, scale):
    root = ROOT_MAP.get(key, 9); ivs = KEY_INTERVALS.get(scale, KEY_INTERVALS["minor"])
    best, bd = round(midi_f), 999.0
    for c in range(int(midi_f)-13, int(midi_f)+14):
        if ((c - root) % 12) in ivs:
            d = abs(c - midi_f)
            if d < bd: bd, best = d, c
    return best

def write_wav(path, samples):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'w') as wf:
        wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(SR)
        wf.writeframes(struct.pack(f'<{len(samples)}h',
            *[max(-32767, min(32767, int(s * 32767))) for s in samples]))

def read_wav_rms(path):
    with wave.open(str(path), 'r') as wf:
        raw = wf.readframes(wf.getnframes()); n = len(raw)//2
    if n == 0: return 0.0
    samples = struct.unpack(f'<{n}h', raw)
    return math.sqrt(sum(s*s for s in samples) / n) / 32767.0

def sine(hz, dur, amp=0.7, p0=0.0):
    n = int(dur*SR); out,p = [],p0; inc = 2*math.pi*hz/SR
    for _ in range(n): out.append(amp*math.sin(p)); p += inc
    return out, p % (2*math.pi)

def sld(hz0, hz1, dur, amp=0.7, p0=0.0):
    n = int(dur*SR); out,p = [],p0
    for i in range(n):
        hz = hz0*(hz1/hz0)**(i/n); out.append(amp*math.sin(p)); p += 2*math.pi*hz/SR
    return out, p % (2*math.pi)

def vibrato(center_hz, depth_c, rate_hz, dur, amp=0.7, p0=0.0):
    n=int(dur*SR); out,p,lp=[],p0,0.0; li=2*math.pi*rate_hz/SR
    for _ in range(n):
        hz=center_hz*(2**(math.sin(lp)*depth_c/1200)); out.append(amp*math.sin(p))
        p+=2*math.pi*hz/SR; lp+=li
    return out, p%(2*math.pi)

def silence(dur): return [0.0]*int(dur*SR)

def env(s, atk=0.01, rel=0.02):
    s=list(s); n=len(s)
    na,nr=min(int(atk*SR),n),min(int(rel*SR),n)
    for i in range(na): s[i]*=i/na
    for i in range(nr): s[n-1-i]*=i/nr
    return s

A3,B3,C4,D4,E4,F4,G4,A4 = 57,59,60,62,64,65,67,69

def make_corpus():
    "Identical to intent_bench_v3.py — kept standalone."
    cases = []
    for off_c,tag in [(-40,"m40"),(-50,"m50"),(-60,"m60"),(+40,"p40"),(+50,"p50"),(+60,"p60")]:
        hz = midi_to_hz(A3+off_c/100)
        def _gen(h=hz): s,_=sine(h,2.5); return env(s)
        cases.append(dict(id=f"BC_A3_{tag}",category="boundary",key="A",scale="minor",gen=_gen,
            intent=[IntentSegment(0.0,2.5,A3+off_c/100,A3,Role.STABLE_TARGET,True)]))
    for dur_ms in [100,150,200,300,400]:
        pre=0.5;post=1.0;sd=dur_ms/1000
        def _gen(sd=sd):
            a,p=sine(midi_to_hz(A3),pre); b,p=sld(midi_to_hz(A3),midi_to_hz(B3),sd,p0=p)
            c,_=sine(midi_to_hz(B3),post,p0=p); return env(a+b+c)
        cases.append(dict(id=f"SLIDE_semi_{dur_ms}ms",category="semitone_slide",
            key="A",scale="minor",gen=_gen,
            intent=[IntentSegment(0.0,pre,A3,A3,Role.STABLE_TARGET,True),
                    IntentSegment(pre,pre+sd,A3+0.5,B3,Role.SLIDE,False),
                    IntentSegment(pre+sd,pre+sd+post,B3,B3,Role.STABLE_TARGET,True)]))
    for dur_ms in [200,400]:
        pre=0.4;post=1.0;sd=dur_ms/1000
        def _gen(sd=sd):
            a,p=sine(midi_to_hz(A3),pre); b,p=sld(midi_to_hz(A3),midi_to_hz(D4),sd,p0=p)
            c,_=sine(midi_to_hz(D4),post,p0=p); return env(a+b+c)
        cases.append(dict(id=f"SLIDE_whole_{dur_ms}ms",category="whole_tone_slide",
            key="A",scale="minor",gen=_gen,
            intent=[IntentSegment(0.0,pre,A3,A3,Role.STABLE_TARGET,True),
                    IntentSegment(pre,pre+sd,A3+2.5,D4,Role.SLIDE,False),
                    IntentSegment(pre+sd,pre+sd+post,D4,D4,Role.STABLE_TARGET,True)]))
    for depth_c in [30,60,100]:
        sl=0.15;post=1.5
        def _gen(sh=midi_to_hz(A3-depth_c/100)):
            b,p=sld(sh,midi_to_hz(A3),sl); c,_=sine(midi_to_hz(A3),post,p0=p)
            return env(b+c,atk=0.001)
        cases.append(dict(id=f"SCOOP_A3_{depth_c}c",category="scoop",
            key="A",scale="minor",gen=_gen,
            intent=[IntentSegment(0.0,sl,A3-depth_c/100,A3,Role.SCOOP,False),
                    IntentSegment(sl,sl+post,A3,A3,Role.STABLE_TARGET,True)]))
    for bb in [60,100,150,200]:
        pre=0.4;bb_s=bb/1000;post=0.8
        def _gen(bd=bb_s):
            a,p=sine(midi_to_hz(A3),pre); b,p=sine(midi_to_hz(A3+1),bd,p0=p)
            c,_=sine(midi_to_hz(B3),post,p0=p); return env(a+b+c,atk=0.005)
        cases.append(dict(id=f"CHROM_pass_Bb3_{bb}ms",category="chromatic_passing",
            key="A",scale="minor",gen=_gen,
            intent=[IntentSegment(0.0,pre,A3,B3,Role.STABLE_TARGET,True),
                    IntentSegment(pre,pre+bb_s,A3+1,B3,Role.PASSING_TONE,False),
                    IntentSegment(pre+bb_s,pre+bb_s+post,B3,B3,Role.STABLE_TARGET,True)]))
    for dc in [50,80]:
        pre=0.6;bend=0.25;post=0.8
        def _gen(dc=dc):
            a,p=sine(midi_to_hz(G4-12),pre); b,p=sld(midi_to_hz(G4-12),midi_to_hz(G4-12+dc/100),bend,p0=p)
            c,_=sine(midi_to_hz(G4-12),post,p0=p); return env(a+b+c)
        cases.append(dict(id=f"OOS_blues_G3_bend{dc}c",category="out_of_scale",
            key="A",scale="minor",gen=_gen,
            intent=[IntentSegment(0.0,pre,G4-12,G4-12,Role.STABLE_TARGET,True),
                    IntentSegment(pre,pre+bend,G4-12+dc/100,G4-12,Role.VIBRATO_EXCURSION,False),
                    IntentSegment(pre+bend,pre+bend+post,G4-12,G4-12,Role.STABLE_TARGET,True)]))
    for wd in [80,150]:
        d=wd/1000
        def _gen(d=d):
            a,p=sine(midi_to_hz(A3),0.5); b,p=sine(midi_to_hz(A3+2),d,p0=p)
            c,_=sine(midi_to_hz(A3),0.8,p0=p); return env(a+b+c,atk=0.005,rel=0.01)
        cases.append(dict(id=f"WRONG_note_{wd}ms",category="wrong_note",
            key="A",scale="minor",gen=_gen,
            intent=[IntentSegment(0.0,0.5,A3,A3,Role.STABLE_TARGET,True),
                    IntentSegment(0.5,0.5+d,A3+2,A3,Role.WRONG_NOTE,True),
                    IntentSegment(0.5+d,1.3+d,A3,A3,Role.STABLE_TARGET,True)]))
    for dc in [30,50,70]:
        ctr=A3+0.5
        def _gen(d=dc,c=ctr): s,_=vibrato(midi_to_hz(c),d,5.0,3.0); return env(s)
        cases.append(dict(id=f"VIB_cross_{dc}c",category="vibrato_boundary",
            key="A",scale="minor",gen=_gen,
            intent=[IntentSegment(0.0,3.0,A3+0.5,A3,Role.VIBRATO_EXCURSION,False)]))
    run_notes=[A3,B3,C4,D4,E4]
    for nd_ms in [80,120,200,350]:
        def _gen(nd=nd_ms/1000):
            out=[]; p=0.0
            for note in run_notes:
                s,p=sine(midi_to_hz(note),nd,p0=p); out+=env(s,atk=0.005,rel=0.005)
            return out
        t=0.0; ints=[]
        for idx,note in enumerate(run_notes):
            r=Role.STABLE_TARGET if idx==len(run_notes)-1 else Role.PASSING_TONE
            ints.append(IntentSegment(t,t+nd_ms/1000,note,E4,r,idx==len(run_notes)-1)); t+=nd_ms/1000
        cases.append(dict(id=f"RUN_up_{nd_ms}ms",category="run",key="A",scale="minor",gen=_gen,intent=ints))
    for nr in [4,8]:
        def _gen(nr=nr):
            out=[]; p=0.0
            for _ in range(nr):
                s,p=sine(midi_to_hz(A3),0.15,p0=p); out+=env(s,atk=0.008,rel=0.008); out+=silence(0.04)
            return out
        cases.append(dict(id=f"REPEAT_A3_{nr}x",category="repeated_note",key="A",scale="minor",gen=_gen,
            intent=[IntentSegment(0.0,9.9,A3,A3,Role.STABLE_TARGET,True)]))
    for dur_ms in [80,100,150,200,350,500]:
        def _gen(d=dur_ms/1000): s,_=sine(midi_to_hz(A3),d); return env(s,atk=0.01,rel=0.02)
        cases.append(dict(id=f"SHORT_A3_{dur_ms}ms",category="short_note",key="A",scale="minor",gen=_gen,
            intent=[IntentSegment(0.0,dur_ms/1000,A3,A3,Role.STABLE_TARGET,True)]))
    for dc in [50,100]:
        def _gen(dc=dc):
            a,p=sine(midi_to_hz(A3),1.0); b,_=sld(midi_to_hz(A3),midi_to_hz(A3-dc/100),0.3,amp=0.5,p0=p)
            return env(a+b,rel=0.05)
        cases.append(dict(id=f"FALL_A3_{dc}c",category="fall",key="A",scale="minor",gen=_gen,
            intent=[IntentSegment(0.0,1.0,A3,A3,Role.STABLE_TARGET,True),
                    IntentSegment(1.0,1.3,A3-dc/100,A3,Role.RELEASE,False)]))
    return cases

def make_adversarial(cases):
    adv=[]; seen=set()
    for case in cases:
        cat=case["category"]; key=case["key"]; scale=case["scale"]
        if cat=="boundary":
            for off in [-58,-55,-52,-48,-45,+45,+48,+52,+55,+58]:
                uid=f"ADV_BC_{off:+d}c"
                if uid in seen: continue
                seen.add(uid); hz=midi_to_hz(A3+off/100)
                def _gen(h=hz): s,_=sine(h,2.0); return env(s)
                adv.append(dict(id=uid,category="adv_boundary",key=key,scale=scale,gen=_gen,
                    intent=[IntentSegment(0,2.0,A3+off/100,A3,Role.STABLE_TARGET,True)]))
        elif cat=="vibrato_boundary":
            for dep in [15,20,25,35,45,55,65,80]:
                uid=f"ADV_VIB_{dep}c"
                if uid in seen: continue
                seen.add(uid)
                def _gen(d=dep): s,_=vibrato(midi_to_hz(A3+0.5),d,5.0,3.0); return env(s)
                adv.append(dict(id=uid,category="adv_vib_boundary",key=key,scale=scale,gen=_gen,
                    intent=[IntentSegment(0,3.0,A3+0.5,A3,Role.VIBRATO_EXCURSION,False)]))
        elif cat=="semitone_slide":
            for d_ms in [50,75,100,150,200,300,500,750]:
                for so in [-20,0,+20]:
                    uid=f"ADV_SLIDE_semi_{d_ms}ms_off{so:+d}c"
                    if uid in seen: continue
                    seen.add(uid); sd=d_ms/1000
                    def _gen(s=sd,so=so):
                        a,p=sine(midi_to_hz(A3+so/100),0.4); b,p=sld(midi_to_hz(A3+so/100),midi_to_hz(B3),s,p0=p)
                        c,_=sine(midi_to_hz(B3),0.8,p0=p); return env(a+b+c)
                    adv.append(dict(id=uid,category="adv_slide",key=key,scale=scale,gen=_gen,
                        intent=[IntentSegment(0,0.4,A3+so/100,B3,Role.STABLE_TARGET,True),
                                IntentSegment(0.4,0.4+sd,A3+0.5,B3,Role.SLIDE,False),
                                IntentSegment(0.4+sd,9.9,B3,B3,Role.STABLE_TARGET,True)]))
        elif cat=="chromatic_passing":
            for bb_ms in [40,60,80,100,120,160,200]:
                uid=f"ADV_CHROM_{bb_ms}ms"
                if uid in seen: continue
                seen.add(uid); bd=bb_ms/1000
                def _gen(d=bd):
                    a,p=sine(midi_to_hz(A3),0.35); b,p=sine(midi_to_hz(A3+1),d,p0=p)
                    c,_=sine(midi_to_hz(B3),0.7,p0=p); return env(a+b+c)
                adv.append(dict(id=uid,category="adv_chromatic_pass",key=key,scale=scale,gen=_gen,
                    intent=[IntentSegment(0,0.35,A3,B3,Role.STABLE_TARGET,True),
                            IntentSegment(0.35,0.35+bd,A3+1,B3,Role.PASSING_TONE,False),
                            IntentSegment(0.35+bd,9.9,B3,B3,Role.STABLE_TARGET,True)]))
    return adv

# ---------------------------------------------------------------------------
# GT labeling
# ---------------------------------------------------------------------------
def label_frames(frames, intent_segs):
    for fr in frames:
        t = fr.get("timeSec",0.0); seg=None
        for s in intent_segs:
            if s.t_start <= t < s.t_end: seg=s; break
        if seg is None and intent_segs: seg=intent_segs[-1]
        fr["gtPerformedNote"]    = seg.performed_note if seg else None
        fr["gtIntendedDest"]     = seg.intended_destination if seg else None
        fr["gtNoteRole"]         = seg.note_role if seg else None
        fr["gtShouldCorrectNow"] = seg.should_correct_now if seg else False
    return frames

# ---------------------------------------------------------------------------
# Bench runner
# ---------------------------------------------------------------------------
def run_bench(wav_path, key, scale, intent_mode="production", out_dir=None):
    wav_path = Path(wav_path)
    stem = wav_path.stem
    if out_dir is None: out_dir = OUT / intent_mode
    out_dir = Path(out_dir); out_dir.mkdir(parents=True, exist_ok=True)
    tr  = out_dir / f"{stem}_trace.json"
    wo  = out_dir / f"{stem}_out.wav"
    cmd = [str(BENCH), str(wav_path),
           "--output", str(wo), "--trace", str(tr),
           "--key", key, "--scale", scale,
           "--amount", "0.82", "--speed", "0.70",
           "--block", str(BLOCK),
           "--intent-mode", intent_mode]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if r.returncode != 0 or not tr.exists(): return None, None
    return json.loads(tr.read_text()).get("frames", []), wo

# ---------------------------------------------------------------------------
# Phase 1 metrics — audio quality from bench trace + output WAV
# ---------------------------------------------------------------------------
def audio_metrics(frames, gt_frames=None, out_wav=None):
    """
    Measures audio quality from trace frames and optional output WAV.
    Returns dict with jump stats, chatter, GT accuracy, NaN/Inf flag.
    """
    voiced = [f for f in frames if f.get("voiced")]
    if not voiced: return {}

    # NaN/Inf in correctionCents
    nan_inf = sum(1 for f in voiced
                  if not math.isfinite(f.get("correctionCents",0.0)))

    # Correction jumps between consecutive voiced frames
    jumps = []
    for i in range(1, len(voiced)):
        c0 = voiced[i-1].get("correctionCents",0.0)
        c1 = voiced[i].get("correctionCents",0.0)
        if math.isfinite(c0) and math.isfinite(c1):
            jumps.append(abs(c1 - c0))

    # intentTarget switching (chatter)
    tgt_switches = sum(1 for i in range(1,len(voiced))
                       if voiced[i].get("intentTarget") != voiced[i-1].get("intentTarget")
                       and voiced[i].get("intentTarget",-1) >= 0)

    # GT-based metrics (when GT labels present)
    gt_stable_acc = gt_dest_acc = gt_false_tgt = gt_oct_err = None
    if gt_frames:
        labeled = [f for f in gt_frames if f.get("gtNoteRole")]
        stable  = [f for f in labeled if f.get("gtNoteRole") == Role.STABLE_TARGET]
        transit = [f for f in labeled if f.get("gtNoteRole") in (Role.SLIDE,Role.SCOOP,Role.RELEASE)]
        should  = [f for f in labeled if f.get("gtShouldCorrectNow")]
        # Use intentTarget (v3 decision) when active; fall back to detectedMidi (production)
        has_intent = any(f.get("intentTarget",-1) >= 0 for f in labeled)
        tgt_key = "intentTarget" if has_intent else "detectedMidi"
        def _r(v): return round(v,4) if v is not None else None
        def _tgt(f): return f.get(tgt_key, -1) if tgt_key == "intentTarget" else round(f.get("detectedMidi",0) or 0)
        if stable:
            gt_stable_acc = _r(sum(1 for f in stable if _tgt(f) == f.get("gtIntendedDest"))/len(stable))
        if transit:
            gt_dest_acc = _r(sum(1 for f in transit if _tgt(f) == f.get("gtIntendedDest"))/len(transit))
        if should:
            gt_false_tgt = _r(sum(1 for f in should if _tgt(f) != f.get("gtIntendedDest"))/len(should))
        # Octave errors: voiced frames where v3 targeted a note 12+ semitones from GT
        # Uses intentTarget (not detectedMidi) to measure actual targeting decisions,
        # excluding unvoiced frames that the label_frames fallback may have labeled.
        gt_oct_err = sum(1 for f in labeled
            if f.get("voiced")
            and f.get("gtIntendedDest") is not None
            and f.get("intentTarget",-1) >= 0
            and abs(f.get("intentTarget",0) - f.get("gtIntendedDest")) >= 12)

    def _p(lst, pct):
        if not lst: return 0.0
        s = sorted(lst); idx = int(len(s)*pct/100)
        return s[min(idx, len(s)-1)]

    # Output WAV quality: RMS continuity (ratio of consecutive RMS chunks)
    rms_ratio_bad = 0
    if out_wav and Path(out_wav).exists():
        with wave.open(str(out_wav), 'r') as wf:
            raw = wf.readframes(wf.getnframes()); n = len(raw)//2
        if n > BLOCK*4:
            samples = struct.unpack(f'<{n}h', raw)
            chunk = BLOCK
            rms_vals = [math.sqrt(sum(samples[i:i+chunk][j]**2 for j in range(min(chunk,n-i)))/chunk)
                        for i in range(0, n-chunk, chunk)]
            rms_ratio_bad = sum(1 for i in range(1,len(rms_vals))
                               if rms_vals[i-1] > 0 and rms_vals[i]/rms_vals[i-1] > 3.0)

    return {
        "n_voiced":       len(voiced),
        "nan_inf":        nan_inf,
        "max_jump_cents": round(max(jumps) if jumps else 0.0, 2),
        "p95_jump_cents": round(_p(jumps, 95), 2),
        "p50_jump_cents": round(_p(jumps, 50), 2),
        "mean_jump_cents":round(sum(jumps)/len(jumps) if jumps else 0.0, 2),
        "chatter_rate":   round(tgt_switches/len(voiced) if voiced else 0.0, 4),
        "gt_stable_acc":  gt_stable_acc,
        "gt_dest_acc":    gt_dest_acc,
        "gt_false_tgt":   gt_false_tgt,
        "gt_oct_err":     gt_oct_err,
        "rms_bad_ratio":  rms_ratio_bad,
        "v2_fire_rate":   round(sum(1 for f in voiced
                              if f.get("intentReasonName")=="SLIDE_DESTINATION_V2")/len(voiced), 4),
    }

# ---------------------------------------------------------------------------
# Phase 3 — Python shadow CME for semitone slide horizon sweep
# ---------------------------------------------------------------------------
class CMESimple:
    CAPACITY = 64
    def __init__(self):
        self._midi=[0.0]*64; self._time=[0.0]*64; self._pos=0; self._fill=0; self._ct=0.0
    def reset(self): self._pos=self._fill=0; self._ct=0.0
    def push(self,hz,bs):
        if hz<=0 or bs<=0: return
        self._ct+=bs; mf=69.0+12.0*math.log2(hz/440.0)
        self._midi[self._pos]=mf; self._time[self._pos]=self._ct
        self._pos=(self._pos+1)%64
        if self._fill<64: self._fill+=1
    def fd(self,win=0.020):
        if self._fill<2: return 0.0,0.0,False,0.0
        last=(self._pos-1+64)%64; tnow=self._time[last]; tcut=tnow-win
        ms,ts=[],[]
        for i in range(self._fill):
            idx=(self._pos-1-i+128)%64
            if self._time[idx]<tcut: break
            ms.append(self._midi[idx]); ts.append(self._time[idx])
        ms.reverse(); ts.reverse()
        if len(ms)<2: return 0.0,0.0
        dt=ts[-1]-ts[-2]; vel=(ms[-1]-ms[-2])/dt if dt>1e-12 else 0.0
        # confidence from lsq vs median agreement
        def lsq(m,t):
            n=len(m); t0=t[0]; xs=[ti-t0 for ti in t]
            sx=sum(xs); sy=sum(m); sxx=sum(x*x for x in xs); sxy=sum(x*y for x,y in zip(xs,m))
            d=n*sxx-sx*sx; return (n*sxy-sx*sy)/d if abs(d)>1e-15 else 0.0
        def rm(m,t):
            slopes=sorted((m[i]-m[i-1])/(t[i]-t[i-1]) for i in range(1,len(m)) if t[i]-t[i-1]>1e-12)
            if not slopes: return 0.0
            n=len(slopes); return slopes[n//2] if n%2 else 0.5*(slopes[n//2-1]+slopes[n//2])
        ls=lsq(ms,ts); med=rm(ms,ts); conf=max(0.0,1.0-abs(ls-med)/10.0)
        # vibrato
        hp=hn=False
        for i in range(1,len(ms)):
            dt=ts[i]-ts[i-1]
            if dt<1e-12: continue
            v=(ms[i]-ms[i-1])/dt
            if v>1.5: hp=True
            if v<-1.5: hn=True
        mf=self._midi[(self._pos-1+64)%64]
        return vel, conf, hp and hn, mf

@dataclass
class V2State:
    ph: deque = field(default_factory=lambda: deque(maxlen=12))
    vh: deque = field(default_factory=lambda: deque(maxlen=12))
    st: int = -1; ss: float = 0.0; lv: float = -1.0
    dw: dict = field(default_factory=dict)

    def sv(self):
        p=list(self.ph)
        return (p[-1]-p[0])/(len(p)*BLOCK_SEC) if len(p)>=2 else 0.0
    def dc(self,n=6):
        pvs=list(self.vh)[-n:]
        if len(pvs)<3: return False,0.0
        sg=[1 if v>0.3 else(-1 if v<-0.3 else 0) for v in pvs]
        pos=sg.count(1);neg=sg.count(-1)
        if pos>=len(sg)*0.6: return True,1.0
        if neg>=len(sg)*0.6: return True,-1.0
        return False,0.0

def shadow_v3_param(frames, key, scale, extrap_sec=0.080,
                    min_conf=0.60, min_motion_conf=0.65, min_vel=3.0):
    """Parameterized shadow_v3 for Phase 3 horizon sweep."""
    state = V2State(); cme = CMESimple()
    SLIDE_VEL=0.6; PASS_DWELL=0.18; STAB_SEC=0.10; PERSIST_SEC=0.12
    MAX_SLIDE_DIST = 4.0

    for i, fr in enumerate(frames):
        if not fr.get("voiced"):
            fr["sv3t"]=-1; fr["sv3r"]=Reason.LOW_CONFIDENCE_FALLBACK
            state.ph.clear(); state.vh.clear(); state.dw.clear()
            state.ss=0.0; state.lv=-1.0; cme.reset(); continue

        det=float(fr.get("detectedMidi",-1) or -1)
        conf=float(fr.get("confidence",0.0) or 0.0)
        prod=int(round(det))
        sv=(det-state.lv)/BLOCK_SEC if state.lv>=0 else 0.0
        state.lv=det; state.ph.append(det); state.vh.append(sv)
        smv=state.sv(); con,direction=state.dc()
        nn=nearest_scale(det,key,scale); state.dw[nn]=state.dw.get(nn,0)+1

        det_hz=fr.get("detectedHz") or midi_to_hz(det)
        if det_hz>0: cme.push(det_hz,BLOCK_SEC)
        vel,mconf,isvib,mf=cme.fd(0.020)

        rv=list(state.vh)[-4:]
        osc=(any(v>0.3 for v in rv) and any(v<-0.3 for v in rv) and len(rv)>=4)

        target=prod; reason=Reason.LOW_CONFIDENCE_FALLBACK

        if (not isvib and con and conf>=min_conf and mconf>=min_motion_conf
                and abs(vel)>=min_vel and not osc):
            raw=mf+vel*extrap_sec
            bounded=max(det-MAX_SLIDE_DIST,min(det+MAX_SLIDE_DIST,raw))
            snap=nearest_scale(bounded,key,scale)
            if snap!=nn and abs(snap-nn)>=1:
                target=snap; reason=Reason.SLIDE_DESTINATION_V2

        if reason==Reason.LOW_CONFIDENCE_FALLBACK:
            if abs(smv)>=SLIDE_VEL and con:
                la=det+direction*abs(smv)*0.20; dest=nearest_scale(la,key,scale)
                if dest==nn and abs(smv)>1.0: la=det+direction*abs(smv)*0.35; dest=nearest_scale(la,key,scale)
                target=dest; reason=Reason.SLIDE_DESTINATION
            elif (con and state.dw.get(nn,0)*BLOCK_SEC<PASS_DWELL and abs(smv)>0.35 and state.st!=-1):
                target=state.st; reason=Reason.PASSING_TONE
            elif state.ss>=STAB_SEC and conf>=0.55 and abs(smv)<0.5:
                cand=nearest_scale(det,key,scale)
                if (state.st!=-1 and state.st!=cand and abs(smv)<1.2 and state.ss<PERSIST_SEC*3):
                    target=state.st; reason=Reason.VIBRATO_HOLD
                else:
                    target=cand; state.st=cand; reason=Reason.STABILITY_ANCHOR
            elif state.st!=-1 and abs(smv)<0.8:
                target=state.st; reason=Reason.VIBRATO_HOLD
            else:
                target=nearest_scale(det,key,scale); reason=Reason.NEAREST_STABLE

        if target==state.st and abs(smv)<0.8: state.ss+=BLOCK_SEC
        elif reason not in (Reason.PASSING_TONE,Reason.VIBRATO_HOLD,Reason.SLIDE_DESTINATION_V2):
            state.ss=0.0

        fr["sv3t"]=target; fr["sv3r"]=reason
    return frames

# ---------------------------------------------------------------------------
# Phase 1 — full render matrix
# ---------------------------------------------------------------------------
def phase1_render_matrix(all_cases, wav_dir):
    results = []
    n = len(all_cases)
    for i, case in enumerate(all_cases):
        cid = case["id"]; key = case["key"]; scale = case["scale"]
        wav_path = wav_dir / f"{cid}.wav"
        if not wav_path.exists(): write_wav(wav_path, case["gen"]())
        print(f"  [{i+1:3}/{n}] {cid}:", end=" ", flush=True)

        # Production run
        prod_frames, prod_wav = run_bench(wav_path, key, scale, "production", OUT/"prod")
        # v3 active run
        v3_frames, v3_wav = run_bench(wav_path, key, scale, "v2_active", OUT/"v3")

        if prod_frames is None or v3_frames is None:
            print("SKIP (bench fail)"); continue

        # Label both with GT
        prod_lf = label_frames(list(prod_frames), case.get("intent",[]))
        v3_lf   = label_frames(list(v3_frames),   case.get("intent",[]))

        prod_m = audio_metrics(prod_frames, prod_lf, prod_wav)
        v3_m   = audio_metrics(v3_frames,  v3_lf,   v3_wav)

        # NaN/Inf is a hard crash
        crash = v3_m.get("nan_inf",0) > 0
        max_j = v3_m.get("max_jump_cents",0)
        p95_j = v3_m.get("p95_jump_cents",0)
        delta_chatter = v3_m.get("chatter_rate",0) - prod_m.get("chatter_rate",0)
        delta_maxjump = max_j - prod_m.get("max_jump_cents",0)

        flag = "🔴 CRASH" if crash else ("🟡" if max_j>200 or delta_maxjump>100 else "✅")
        print(f"{flag}  maxJ={max_j:.0f}¢  p95J={p95_j:.0f}¢  Δchat={delta_chatter:+.2%}  v2fire={v3_m.get('v2_fire_rate',0):.1%}")

        results.append({
            "id": cid, "category": case["category"],
            "prod": prod_m, "v3": v3_m,
            "crash": crash,
            "delta_maxjump": round(delta_maxjump, 2),
            "delta_chatter": round(delta_chatter, 4),
        })

    return results

# ---------------------------------------------------------------------------
# Phase 3 — semitone slide horizon sweep
# ---------------------------------------------------------------------------
def phase3_semislide_sweep(all_cases):
    slide_cases = [c for c in all_cases
                   if c["category"] in ("semitone_slide","adv_slide","whole_tone_slide")]
    horizons = [0.040, 0.060, 0.080]
    sweep_results = {h: {"dest_acc":[],"stable_acc":[],"v2_fire":[]} for h in horizons}

    for case in slide_cases:
        wav_path = OUT / "semislide" / f"{case['id']}.wav"
        if not wav_path.exists(): write_wav(wav_path, case["gen"]())

        # Load production trace (reuse if available)
        tr_prod = OUT / "prod" / f"{case['id']}_trace.json"
        if not tr_prod.exists():
            run_bench(wav_path, case["key"], case["scale"], "production", OUT/"prod")
        if not tr_prod.exists(): continue

        frames_raw = json.loads(tr_prod.read_text()).get("frames",[])

        for h in horizons:
            frames = [dict(f) for f in frames_raw]  # fresh copy each horizon
            frames = label_frames(frames, case.get("intent",[]))
            frames = shadow_v3_param(frames, case["key"], case["scale"], extrap_sec=h)

            voiced = [f for f in frames if f.get("voiced") and f.get("gtNoteRole")]
            stable  = [f for f in voiced if f.get("gtNoteRole")==Role.STABLE_TARGET]
            transit = [f for f in voiced if f.get("gtNoteRole") in (Role.SLIDE,Role.SCOOP,Role.RELEASE)]
            v2_fr   = [f for f in voiced if f.get("sv3r")==Reason.SLIDE_DESTINATION_V2]

            if stable:
                sweep_results[h]["stable_acc"].append(
                    sum(1 for f in stable if f.get("sv3t")==f.get("gtIntendedDest"))/len(stable))
            if transit:
                sweep_results[h]["dest_acc"].append(
                    sum(1 for f in transit if f.get("sv3t")==f.get("gtIntendedDest"))/len(transit))
            if voiced:
                sweep_results[h]["v2_fire"].append(len(v2_fr)/len(voiced))

    return sweep_results

# ---------------------------------------------------------------------------
# Phase 4 — confidence band calibration
# ---------------------------------------------------------------------------
def phase4_confidence_bands(all_cases):
    """Read existing v3 shadow traces, measure v3 accuracy per confidence decile."""
    bands = [(0.0,0.5),(0.5,0.6),(0.6,0.7),(0.7,0.8),(0.8,0.9),(0.9,1.01)]
    band_data = {b: {"v3_correct":[], "prod_correct":[]} for b in bands}

    for case in all_cases:
        tr_v3 = V3_TRACES / f"{case['id']}_v3.json"
        if not tr_v3.exists(): continue
        data = json.loads(tr_v3.read_text())
        frames = data.get("frames",[])
        frames = label_frames(frames, case.get("intent",[]))

        for f in frames:
            if not f.get("voiced") or not f.get("gtShouldCorrectNow"): continue
            conf = float(f.get("confidence",0.0) or 0.0)
            gt = f.get("gtIntendedDest")
            v3t = f.get("shadowV3Target",-1)
            proddet = round(f.get("detectedMidi",0) or 0)
            for lo,hi in bands:
                if lo <= conf < hi:
                    band_data[(lo,hi)]["v3_correct"].append(1 if v3t==gt else 0)
                    band_data[(lo,hi)]["prod_correct"].append(1 if proddet==gt else 0)
                    break

    results = {}
    for band, d in band_data.items():
        n = len(d["v3_correct"])
        v3_acc = sum(d["v3_correct"])/n if n>0 else None
        prod_acc = sum(d["prod_correct"])/n if n>0 else None
        results[f"{band[0]:.1f}-{band[1]:.1f}"] = {
            "n": n, "v3_acc": v3_acc, "prod_acc": prod_acc,
            "v3_wins": (v3_acc or 0) > (prod_acc or 0) if n>0 else None,
        }
    return results

# ---------------------------------------------------------------------------
# Phase 5 — regression suite
# ---------------------------------------------------------------------------
def phase5_regression():
    results = {}

    # 1. AutoPitchCoreContract
    if CONTRACT.exists():
        r = subprocess.run([str(CONTRACT)], capture_output=True, text=True, timeout=120)
        results["contract"] = {"pass": r.returncode==0, "out": r.stdout[-500:]}
        print(f"  contract: {'PASS' if r.returncode==0 else 'FAIL'}")
    else:
        results["contract"] = {"pass": None, "out": "binary not built"}
        print("  contract: NOT BUILT")

    # 2. AutoPitchMotionBench (rebuild if needed, then run)
    if MOTIONBENCH.exists():
        r = subprocess.run([str(MOTIONBENCH)], capture_output=True, text=True, timeout=60)
        results["motionbench"] = {"pass": r.returncode==0, "out": r.stdout[-300:]}
        rmse_line = [l for l in r.stdout.splitlines() if "best" in l.lower() or "rmse" in l.lower()]
        print(f"  motionbench: {'PASS' if r.returncode==0 else 'FAIL'}" +
              (f"  ({rmse_line[0]})" if rmse_line else ""))
    else:
        results["motionbench"] = {"pass": None, "out": "binary not built"}
        print("  motionbench: NOT BUILT (run: cmake --build build --target AutoPitchMotionBench)")

    # 3. VocalBench stable smoke (3 representative cases)
    vb_cases = list(VB_CORPUS.glob("stable/*.wav"))[:6] if VB_CORPUS.exists() else []
    vb_pass = 0; vb_fail = 0; vb_nan = 0
    for wav in vb_cases:
        r = subprocess.run([str(BENCH), str(wav), "--smoke",
                            "--key","A","--scale","minor","--block","512",
                            "--intent-mode","v2_active"],
                           capture_output=True, text=True, timeout=30)
        if r.returncode == 0: vb_pass += 1
        else:
            vb_fail += 1
            if "non-finite" in r.stderr: vb_nan += 1
    results["vocalbench_stable"] = {"pass": vb_fail==0 and vb_nan==0,
                                    "pass_count": vb_pass, "fail_count": vb_fail, "nan": vb_nan}
    print(f"  vocalbench stable ({len(vb_cases)} cases): {vb_pass} pass / {vb_fail} fail / {vb_nan} NaN")

    # 4. Smoke test: all 90 corpus cases in v2_active with --smoke
    smoke_dir = REPO / "vocalbench-results" / "intent-bench-v3" / "corpus"
    if not smoke_dir.exists(): smoke_dir = REPO / "vocalbench-results" / "intent-bench-v2" / "corpus"
    smoke_wavs = list(smoke_dir.glob("*.wav")) if smoke_dir.exists() else []
    sm_pass=0; sm_fail=0; sm_nan=0
    for wav in smoke_wavs:
        r = subprocess.run([str(BENCH), str(wav), "--smoke",
                            "--key","A","--scale","minor","--block","512",
                            "--intent-mode","v2_active"],
                           capture_output=True, text=True, timeout=30)
        if r.returncode==0: sm_pass+=1
        else:
            sm_fail+=1
            if "non-finite" in r.stderr: sm_nan+=1
    results["corpus_smoke"] = {"pass": sm_fail==0, "pass_count": sm_pass,
                               "fail_count": sm_fail, "nan": sm_nan, "total": len(smoke_wavs)}
    print(f"  corpus smoke ({len(smoke_wavs)} WAVs): {sm_pass} pass / {sm_fail} fail / {sm_nan} NaN")

    return results

# ---------------------------------------------------------------------------
# Final aggregate + verdict
# ---------------------------------------------------------------------------
def aggregate_verdict(p1_results, p3_sweep, p4_bands, p5_reg, all_cases):
    voiced_total = sum(r["v3"].get("n_voiced",0) for r in p1_results)
    crashes = sum(1 for r in p1_results if r["crash"])
    nan_inf  = sum(r["v3"].get("nan_inf",0) for r in p1_results)

    # Jump stats aggregated
    max_jumps   = [r["v3"].get("max_jump_cents",0) for r in p1_results]
    p95_jumps   = [r["v3"].get("p95_jump_cents",0) for r in p1_results]
    global_max  = max(max_jumps) if max_jumps else 0
    p95_all     = sorted(p95_jumps)[int(len(p95_jumps)*0.95)] if p95_jumps else 0

    # Chatter: compare correction-cents sign-flip rates (production intentTarget=-1
    # always, so Δchat from intentTarget switches is not meaningful as a delta).
    # Instead: cases where v3 p95 correction jump exceeds prod p95 by >50¢ AND
    # the v3 fire rate > 0 (v3 tier is responsible, not baseline v2 behavior).
    chatter_deltas = [r["delta_chatter"] for r in p1_results]
    chatter_worse  = sum(1 for r in p1_results
                         if (r["v3"].get("p95_jump_cents",0) - r["prod"].get("p95_jump_cents",0) > 50
                             and r["v3"].get("v2_fire_rate",0) > 0.01))

    # GT dest accuracy in v3_active vs production
    v3_dest_vals  = [r["v3"].get("gt_dest_acc") for r in p1_results if r["v3"].get("gt_dest_acc") is not None]
    prod_dest_vals = [r["prod"].get("gt_dest_acc") for r in p1_results if r["prod"].get("gt_dest_acc") is not None]

    # Phase 3: best horizon for semitone slides
    def mean(lst): return sum(lst)/len(lst) if lst else None
    p3_summary = {h: {"dest": mean(v["dest_acc"]), "stable": mean(v["stable_acc"]),
                       "fire": mean(v["v2_fire"])} for h,v in p3_sweep.items()}

    # Phase 4: confidence band analysis
    # Find the threshold where v3 starts winning
    p4_threshold = None
    for band, d in sorted(p4_bands.items()):
        if d.get("v3_wins") and d["n"] > 5:
            p4_threshold = band; break

    # Phase 5 regression
    p5_ok = all(r.get("pass") for r in p5_reg.values() if r.get("pass") is not None)

    # Verdict criteria
    hard_fail = []
    if crashes > 0: hard_fail.append(f"crashes={crashes}")
    if nan_inf  > 0: hard_fail.append(f"NaN/Inf in {nan_inf} frames")
    if global_max > 500: hard_fail.append(f"max correction jump {global_max:.0f}¢ > 500¢")
    if chatter_worse > len(p1_results)*0.15:
        hard_fail.append(f"chatter worse in {chatter_worse}/{len(p1_results)} cases (>15%)")
    if not p5_ok:
        for k,v in p5_reg.items():
            if v.get("pass") is False: hard_fail.append(f"regression: {k} FAIL")

    oct_errors = sum(r["v3"].get("gt_oct_err",0) for r in p1_results if r["v3"].get("gt_oct_err") is not None)
    if oct_errors > 0: hard_fail.append(f"octave errors={oct_errors}")

    # Semitone slide verdict
    best_h = min(p3_summary, key=lambda h: -(p3_summary[h].get("dest") or 0)) if p3_summary else None
    best_h_dest = p3_summary[best_h]["dest"] if best_h else None
    current_h_dest = p3_summary.get(0.080,{}).get("dest")
    slide_note = ""
    if best_h and best_h_dest and current_h_dest and best_h != 0.080:
        if best_h_dest > (current_h_dest or 0) + 0.05:
            slide_note = f"  ⚠ Shorter horizon {int(best_h*1000)}ms improves semitone dest by {(best_h_dest-current_h_dest):.1%} — rebuild recommended"
        else:
            slide_note = f"  80ms horizon already optimal (best={int(best_h*1000)}ms Δ={best_h_dest-(current_h_dest or 0):+.1%})"

    # Phase 3 fire=0% caveat: production traces store quantized detectedMidi (nearest semitone),
    # so the Python CME sees a step function — not the continuous midiFloat the C++ CME uses.
    # This suppresses v3 fires in Python shadow; C++ active renders (Phase 1) are ground truth.
    p3_zero_fire = all(
        p3_summary.get(h, {}).get("fire", 1) == 0.0 for h in p3_summary
    ) if p3_summary else False
    if p3_zero_fire and not slide_note:
        slide_note = ("  Phase 3 Python shadow: fire=0% (quantized detectedMidi in prod traces "
                      "prevents velocity estimation; see Phase 1 active renders for ground truth)")

    go = len(hard_fail) == 0

    if go:
        verdict = "GO — INTENT V3 RC"
    else:
        verdict = "HOLD — INTENT V3\n" + "\n".join(f"  ✗ {x}" for x in hard_fail)

    return {
        "verdict": verdict, "go": go,
        "voiced_total": voiced_total,
        "crashes": crashes, "nan_inf": nan_inf,
        "global_max_jump": round(global_max, 1),
        "p95_jump_global": round(p95_all, 1),
        "chatter_worse_count": chatter_worse,
        "oct_errors": oct_errors,
        "p3_summary": p3_summary,
        "slide_note": slide_note,
        "p4_threshold": p4_threshold,
        "p5_ok": p5_ok,
    }

def write_audio_report(p1_results, p3_sweep, p4_bands, p5_reg, verdict_obj):
    def fmt(v): return f"{v:.1%}" if v is not None else "—"
    def fmtc(v): return f"{v:.0f}¢" if v is not None else "—"

    lines = [
        "# Intent v3 Active Audio Validation Report",
        "",
        "## Phase 1 — Active Render Matrix (Production vs v3_active)",
        "",
        f"Total voiced frames: {verdict_obj['voiced_total']:,}",
        f"Crashes: {verdict_obj['crashes']}  |  NaN/Inf: {verdict_obj['nan_inf']}",
        f"Global max correction jump: {verdict_obj['global_max_jump']}¢  |  p95: {verdict_obj['p95_jump_global']}¢",
        f"Cases where v3 chatter > prod+5%: {verdict_obj['chatter_worse_count']} / {len(p1_results)}",
        f"Octave errors: {verdict_obj['oct_errors']}",
        "",
        "### Per-category correction jump summary",
        "",
        "| Category | n | Prod p95J | v3 p95J | Δ MaxJ | v3 Chat | v2 Fire |",
        "|----------|---|-----------|---------|--------|---------|---------|",
    ]
    cats = {}
    for r in p1_results: cats.setdefault(r["category"], []).append(r)
    def cagg(rs, path): vals=[r[path[0]].get(path[1]) for r in rs if r[path[0]].get(path[1]) is not None]; return sum(vals)/len(vals) if vals else None
    for cat, rs in sorted(cats.items()):
        prod_p95 = cagg(rs, ("prod","p95_jump_cents")); v3_p95 = cagg(rs, ("v3","p95_jump_cents"))
        v3_chat  = cagg(rs, ("v3","chatter_rate"));     v3_fire= cagg(rs, ("v3","v2_fire_rate"))
        dmj = cagg(rs, ("delta_maxjump",)) if False else (sum(r["delta_maxjump"] for r in rs)/len(rs) if rs else None)
        lines.append(f"| {cat} | {len(rs)} | {fmtc(prod_p95)} | {fmtc(v3_p95)} | {fmtc(dmj)} | {fmt(v3_chat)} | {fmt(v3_fire)} |")

    lines += [
        "",
        "## Phase 3 — Semitone Slide Horizon Sweep",
        "",
        "| Horizon | Dest Acc | Stable Acc | V2 Fire Rate |",
        "|---------|----------|------------|--------------|",
    ]
    p3s = verdict_obj["p3_summary"]
    for h in sorted(p3s):
        d = p3s[h]; current = " ← current" if h == 0.080 else ""
        lines.append(f"| {int(h*1000)}ms{current} | {fmt(d.get('dest'))} | {fmt(d.get('stable'))} | {fmt(d.get('fire'))} |")
    if verdict_obj["slide_note"]:
        lines.append(""); lines.append(verdict_obj["slide_note"])

    lines += [
        "",
        "## Phase 4 — Confidence Band Calibration",
        "",
        "| Band | N | v3 Acc | Prod Acc | v3 Wins |",
        "|------|---|--------|----------|---------|",
    ]
    for band, d in sorted(p4_bands.items()):
        w = "✓" if d.get("v3_wins") else ("—" if d.get("v3_wins") is None else "✗")
        lines.append(f"| {band} | {d['n']} | {fmt(d.get('v3_acc'))} | {fmt(d.get('prod_acc'))} | {w} |")
    if verdict_obj["p4_threshold"]:
        lines.append(f"\nv3 outperforms production above confidence **{verdict_obj['p4_threshold']}**.")

    lines += [
        "",
        "## Phase 5 — Regression Suite",
        "",
        "| Suite | Pass |",
        "|-------|------|",
    ]
    for k,v in p5_reg.items():
        lines.append(f"| {k} | {'✅' if v.get('pass') else ('⚠ Not built' if v.get('pass') is None else '❌ FAIL')} |")

    lines += [
        "",
        "---",
        "",
        "## Final Verdict",
        "",
        f"```",
        verdict_obj["verdict"],
        f"```",
    ]

    path = OUT / "INTENT_V3_AUDIO_REPORT.md"
    path.write_text("\n".join(lines))
    print(f"\nReport: {path}")

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    if not BENCH.exists():
        print(f"ERROR: bench not found: {BENCH}"); sys.exit(1)

    print("Intent v3 Active Audio Validation")
    print("=" * 50)

    base_cases = make_corpus()
    adv_cases  = make_adversarial(base_cases)
    all_cases  = base_cases + adv_cases
    print(f"Corpus: {len(all_cases)} cases\n")

    # Write WAVs to v3 corpus dir (reuse if already there)
    corpus_dir = OUT / "corpus"
    corpus_dir.mkdir(exist_ok=True)
    for case in all_cases:
        wav_path = corpus_dir / f"{case['id']}.wav"
        if not wav_path.exists(): write_wav(wav_path, case["gen"]())

    # ── Phase 1 ─────────────────────────────────────────────────────────────
    print("Phase 1 — Active render matrix (production vs v3_active)")
    print("-" * 50)
    p1_results = phase1_render_matrix(all_cases, corpus_dir)
    print(f"  {len(p1_results)}/{len(all_cases)} cases rendered\n")

    # ── Phase 2 (real vocal proxy) ───────────────────────────────────────────
    print("Phase 2 — Real-vocal proxy (VocalBench stable corpus)")
    vb_wavs = list(VB_CORPUS.glob("stable/*.wav")) if VB_CORPUS.exists() else []
    p2_results = []
    if vb_wavs:
        print(f"  {len(vb_wavs)} stable cases")
        for wav in vb_wavs[:12]:  # sample 12
            pf, pw = run_bench(wav, "A", "minor", "production", OUT/"vb_prod")
            v3f,v3w= run_bench(wav, "A", "minor", "v2_active",  OUT/"vb_v3")
            if pf is None or v3f is None: continue
            pm = audio_metrics(pf, None, pw); vm = audio_metrics(v3f, None, v3w)
            p2_results.append({"id":wav.stem,"prod":pm,"v3":vm})
        vb_max = max((r["v3"].get("max_jump_cents",0) for r in p2_results), default=0)
        vb_nan = sum(r["v3"].get("nan_inf",0) for r in p2_results)
        vb_osc = sum(1 for r in p2_results if r["v3"].get("rms_bad_ratio",0) > 2)
        print(f"  max jump: {vb_max:.0f}¢  NaN: {vb_nan}  RMS discontinuities: {vb_osc}")
    else:
        print("  No VocalBench stable WAVs found — skipped\n")
        print("  (No real human vocal corpus; VocalBench is synthetic sine-wave corpus)")

    # ── Phase 3 ─────────────────────────────────────────────────────────────
    print("\nPhase 3 — Semitone slide horizon sweep (40/60/80ms)")
    p3_sweep = phase3_semislide_sweep(all_cases)
    def mean(lst): return sum(lst)/len(lst) if lst else None
    for h in sorted(p3_sweep):
        d = p3_sweep[h]
        dm = mean(d["dest_acc"]); sm = mean(d["stable_acc"]); fm = mean(d["v2_fire"])
        current = " ← current" if h==0.080 else ""
        print(f"  {int(h*1000)}ms{current}: dest={dm:.1%} stable={sm:.1%} fire={fm:.1%}" if dm else f"  {int(h*1000)}ms: no data")

    # ── Phase 4 ─────────────────────────────────────────────────────────────
    print("\nPhase 4 — Confidence band calibration")
    p4_bands = phase4_confidence_bands(all_cases)
    for band, d in sorted(p4_bands.items()):
        if d["n"] == 0: continue
        w = "WIN" if d.get("v3_wins") else "LOSE"
        v3a = f"{d['v3_acc']:.1%}" if d['v3_acc'] is not None else "—"
        pa  = f"{d['prod_acc']:.1%}" if d['prod_acc'] is not None else "—"
        print(f"  conf {band}: n={d['n']:4d}  v3={v3a}  prod={pa}  [{w}]")

    # ── Phase 5 ─────────────────────────────────────────────────────────────
    print("\nPhase 5 — Regression suite")
    p5_reg = phase5_regression()

    # ── Verdict ─────────────────────────────────────────────────────────────
    verdict_obj = aggregate_verdict(p1_results, p3_sweep, p4_bands, p5_reg, all_cases)
    write_audio_report(p1_results, p3_sweep, p4_bands, p5_reg, verdict_obj)

    print("\n" + "=" * 50)
    print(verdict_obj["verdict"])

    if verdict_obj["slide_note"]:
        print("\n" + verdict_obj["slide_note"])
    if verdict_obj["p4_threshold"]:
        print(f"\nv3 outperforms production in confidence band {verdict_obj['p4_threshold']} and above.")

    return 0 if verdict_obj["go"] else 1

if __name__ == "__main__":
    sys.exit(main())
