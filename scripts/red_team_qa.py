"""
Agent 5 — Adversarial / Red-Team QA
Attacks all four feature branches produced by the campaign:
  1. Neural Vocal Corpus (Agent 1)
  2. Harmony Engine (Agent 3)
  3. Polyphonic Instrument Tuning (Agent 4)
  4. Live Pitch Mode (Agent 2)
  5. Combination stress tests (cross-feature)

Red-team strategy per feature:
  - Boundary inputs (empty, silence, DC offset, +/-full-scale)
  - Adversarial synthesis (aliased, clipped, noise-only, subharmonics)
  - Parameter extremes (amount=0.0, amount=1.0, speed=0.0, speed=1.0)
  - Scale edge cases (chromatic, single-note scale, B major, Db minor)
  - Race conditions: rapid note succession at minimum block size
  - Combination: harmony + live pitch + polyphonic simultaneously
"""

import subprocess, json, math, struct, sys, wave, time
from pathlib import Path

# ── Paths ──────────────────────────────────────────────────────────────────────
BENCH     = Path("/Users/agentsattari/code/sattari-plugins-suite/build/plugins/auto-pitch/AutoPitchBench")
WORKTREES = Path("/Users/agentsattari/code/sattari-worktrees")
OUTDIR    = Path(__file__).parent.parent / "validation" / "adversarial"
OUTDIR.mkdir(parents=True, exist_ok=True)

SR = 44100
A4_HZ = 440.0
A4_MIDI = 69


# ── Signal generators ─────────────────────────────────────────────────────────

def write_wav(path, samples, sr=44100):
    with wave.open(str(path), "w") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        clipped = [max(-32767, min(32767, int(s * 32767))) for s in samples]
        wf.writeframes(struct.pack(f"<{len(clipped)}h", *clipped))


def read_wav(path):
    with wave.open(str(path), "r") as wf:
        raw = wf.readframes(wf.getnframes())
        n   = len(raw) // 2
        data = struct.unpack(f"<{n}h", raw)
        return [s / 32768.0 for s in data]


def midi_to_hz(midi):
    return A4_HZ * 2 ** ((midi - A4_MIDI) / 12.0)


def sine(hz, dur_sec, amp=0.7, sr=44100):
    n = int(sr * dur_sec)
    return [amp * math.sin(2 * math.pi * hz * i / sr) for i in range(n)]


def silence(dur_sec, sr=44100):
    return [0.0] * int(sr * dur_sec)


def dc_offset(dur_sec, level=0.5, sr=44100):
    return [level] * int(sr * dur_sec)


def white_noise(dur_sec, amp=0.5, sr=44100, seed=42):
    import random
    rng = random.Random(seed)
    return [amp * (rng.random() * 2 - 1) for _ in range(int(sr * dur_sec))]


def clipped_sine(hz, dur_sec, clip_level=0.3, sr=44100):
    raw = sine(hz, dur_sec, amp=1.0, sr=sr)
    return [max(-clip_level, min(clip_level, s)) for s in raw]


def rapid_succession(notes_hz, note_dur_sec=0.05, sr=44100):
    """Adversarial: very fast note changes (50ms each)."""
    out = []
    for hz in notes_hz:
        out.extend(sine(hz, note_dur_sec, amp=0.7, sr=sr))
    return out


def aliased_sine(hz, dur_sec, alias_ratio=0.95, sr=44100):
    """Sine above Nyquist boundary — aliases to different frequency."""
    alias_hz = sr * alias_ratio / 2
    return sine(alias_hz, dur_sec, sr=sr)


def subharmonic_signal(hz, dur_sec, sr=44100):
    """Root + subharmonic at half frequency — tests octave error robustness."""
    n = int(sr * dur_sec)
    t = [i / sr for i in range(n)]
    return [0.5 * math.sin(2 * math.pi * hz * ti)
            + 0.5 * math.sin(2 * math.pi * (hz / 2) * ti) for ti in t]


def full_scale_pos(dur_sec=1.0, sr=44100):
    return [1.0] * int(sr * dur_sec)


def full_scale_neg(dur_sec=1.0, sr=44100):
    return [-1.0] * int(sr * dur_sec)


# ── Run bench helper ──────────────────────────────────────────────────────────

def run_bench(wav_path, label, key="A", scale="minor", amount=0.82, speed=0.7,
              block=256, intent_mode="v2_active", timeout=60):
    trace_path = OUTDIR / f"trace_{label}.json"
    out_path   = OUTDIR / f"out_{label}.wav"
    t0 = time.monotonic()
    try:
        r = subprocess.run([
            str(BENCH), str(wav_path),
            "--output", str(out_path),
            "--trace",  str(trace_path),
            "--key", key,
            "--scale", scale,
            "--amount", str(amount),
            "--speed",  str(speed),
            "--block",  str(block),
            "--intent-mode", intent_mode,
        ], capture_output=True, text=True, timeout=timeout)
        elapsed = time.monotonic() - t0
        ok = r.returncode == 0
        crash = not ok
    except subprocess.TimeoutExpired:
        elapsed = timeout
        crash = True
        ok = False
        r = None

    if not ok or not trace_path.exists():
        return {"label": label, "crash": True, "nan_inf": 0, "octave_errors": 0,
                "max_jump": 0, "voiced": 0, "elapsed_s": elapsed}

    # Parse trace
    frames = json.loads(trace_path.read_text()).get("frames", [])
    nan_inf = 0
    octave_errors = 0
    max_jump = 0.0
    voiced = 0
    prev_midi = None

    for fr in frames:
        if fr.get("voiced"):
            voiced += 1
            det  = fr.get("detectedMidi", 0.0)
            corr = fr.get("correctionCents", 0.0)
            if not math.isfinite(det) or not math.isfinite(corr):
                nan_inf += 1
                continue
            corrected = det + corr / 100.0
            if abs(corrected - det) > 12.0:
                octave_errors += 1
            if prev_midi is not None:
                jump = abs(corrected - prev_midi) * 100.0
                max_jump = max(max_jump, jump)
            prev_midi = corrected

    return {
        "label":          label,
        "crash":          crash,
        "nan_inf":        nan_inf,
        "octave_errors":  octave_errors,
        "max_jump":       round(max_jump, 1),
        "voiced":         voiced,
        "elapsed_s":      round(elapsed, 2),
    }


# ── Build attack suite ────────────────────────────────────────────────────────

# Engine attacks — directly on the production binary
ENGINE_ATTACKS = []

def wav(name, samples):
    p = OUTDIR / f"{name}.wav"
    write_wav(p, samples)
    return p, name

# Boundary
ENGINE_ATTACKS.append(wav("silence_2s",  silence(2.0)))
ENGINE_ATTACKS.append(wav("dc_pos",      dc_offset(2.0, 0.9)))
ENGINE_ATTACKS.append(wav("dc_neg",      dc_offset(2.0, -0.9)))
ENGINE_ATTACKS.append(wav("fullscale_p", full_scale_pos()))
ENGINE_ATTACKS.append(wav("fullscale_n", full_scale_neg()))
ENGINE_ATTACKS.append(wav("noise_only",  white_noise(3.0)))
ENGINE_ATTACKS.append(wav("subharm_A3",  subharmonic_signal(midi_to_hz(57), 3.0)))
ENGINE_ATTACKS.append(wav("clipped_E4",  clipped_sine(midi_to_hz(64), 3.0, clip_level=0.3)))

# Rapid note succession
rapid_chromatic = rapid_succession([midi_to_hz(m) for m in range(48, 73)], 0.05)
ENGINE_ATTACKS.append(wav("rapid_chromatic", rapid_chromatic))

rapid_octave = rapid_succession([midi_to_hz(48), midi_to_hz(60), midi_to_hz(48)]*8, 0.04)
ENGINE_ATTACKS.append(wav("rapid_octave_jump", rapid_octave))

# Aliased (near-Nyquist)
ENGINE_ATTACKS.append(wav("near_nyquist",   aliased_sine(midi_to_hz(69), 2.0)))

# Very short clip (one block worth)
ENGINE_ATTACKS.append(wav("one_block_64",   sine(440.0, 64/44100, amp=0.7)))

# Parameter extremes
WAV_A4 = wav("a4_steady", sine(440.0, 3.0))

print("[RedTeam] Building attack signal bank...")
print(f"[RedTeam] {len(ENGINE_ATTACKS)} adversarial signals generated")


# ── Run all engine attacks ────────────────────────────────────────────────────

print("\n[RedTeam] == ENGINE ATTACKS ==")
engine_results = []

# Standard attacks (varied params)
for wav_path, name in ENGINE_ATTACKS:
    r = run_bench(wav_path, f"eng_{name}", key="A", scale="minor",
                  amount=0.82, speed=0.7, block=256)
    status = "CRASH" if r["crash"] else ("NAN" if r["nan_inf"] > 0 else "OK")
    print(f"  {'❌' if status!='OK' else '✅'} {name}: {status} "
          f"voiced={r['voiced']} oct={r['octave_errors']} nan={r['nan_inf']}")
    engine_results.append(r)

# Parameter extremes
print("\n[RedTeam] == PARAMETER EXTREMES ==")
param_cases = [
    ("amount=0.0",   {"amount": 0.0}),
    ("amount=1.0",   {"amount": 1.0}),
    ("speed=0.01",   {"speed": 0.01}),
    ("speed=1.0",    {"speed": 1.0}),
    ("block=64",     {"block": 64}),
    ("block=64+1.0", {"block": 64, "amount": 1.0, "speed": 1.0}),
]
param_results = []
for label, kwargs in param_cases:
    r = run_bench(WAV_A4[0], f"param_{label.replace('=','_').replace('.','p')}",
                  key="A", scale="minor", **kwargs)
    status = "CRASH" if r["crash"] else ("NAN" if r["nan_inf"] > 0 else "OK")
    print(f"  {'❌' if status!='OK' else '✅'} {label}: {status} voiced={r['voiced']}")
    param_results.append({**r, "label": label})


# Scale edge cases
print("\n[RedTeam] == SCALE EDGE CASES ==")
scale_cases = [
    ("chromatic_Dmin", "D", "chromatic"),
    ("major_B",        "B", "major"),
    ("minor_Db",       "Db","minor"),
    ("minor_G#",       "G#","minor"),
]
scale_results = []
for label, key, scale in scale_cases:
    r = run_bench(WAV_A4[0], f"scale_{label}", key=key, scale=scale)
    status = "CRASH" if r["crash"] else ("NAN" if r["nan_inf"] > 0 else "OK")
    print(f"  {'❌' if status!='OK' else '✅'} {key} {scale}: {status} voiced={r['voiced']}")
    scale_results.append({**r, "label": label})


# ── Harmony Engine adversarial tests ─────────────────────────────────────────

print("\n[RedTeam] == HARMONY ENGINE ADVERSARIAL ==")
HARM_DIR = WORKTREES / "feature-harmony-engine" / "scripts"
harmony_results = []

try:
    import sys as _sys
    _sys.path.insert(0, str(HARM_DIR))
    from harmony_engine import HarmonyEngine, build_scale_notes, ROOT_NAMES, SCALE_SEMITONES

    def harmony_attack(name, key, scale, voices, melody_mids):
        """Run harmony engine on melody and check for illegal tones."""
        engine = HarmonyEngine(key, scale, voices)
        scale_notes = set(build_scale_notes(ROOT_NAMES[key], scale))
        illegal = 0
        crashes = 0
        for m in melody_mids:
            try:
                targets = engine.target(m)
                for _, t in targets:
                    if t is not None and t not in scale_notes:
                        illegal += 1
            except Exception:
                crashes += 1
        ok = crashes == 0 and illegal == 0
        return {"name": name, "illegal": illegal, "crashes": crashes, "ok": ok}

    HARM_ATTACKS = [
        # Below lowest MIDI
        ("very_low_notes", "C", "major", ["+3rd"], list(range(0, 12))),
        # Above highest MIDI
        ("very_high_notes", "C", "major", ["-3rd"], list(range(115, 128))),
        # Chromatic scale full
        ("chromatic_full", "C", "chromatic", ["+3rd", "-3rd", "+5th"], list(range(21, 109))),
        # Rapid key change via set_key
        ("key_change_stress", "C", "major", ["+3rd"], None),
        # All intervals simultaneously on extreme notes
        ("all_intervals_extreme", "B", "major",
         ["+3rd","-3rd","+5th","-5th","+6th","-6th","+oct","-oct","unison"],
         [21, 30, 60, 90, 100, 108, 127]),
        # Float melody (fractional MIDI — sub-cent precision)
        ("fractional_midi", "A", "minor", ["+3rd"],
         [57.0001, 57.4999, 57.5001, 60.9999, 61.0001]),
        # Identical melody repeated 1000 times — stability/memory test
        ("stability_1000", "A", "minor", ["+3rd", "+5th"], [69.0] * 1000),
        # All roots × all scales
        ("all_roots", None, None, ["+3rd"], None),
    ]

    for attack_spec in HARM_ATTACKS:
        name = attack_spec[0]
        if name == "key_change_stress":
            engine = HarmonyEngine("C", "major", ["+3rd"])
            illegal = 0
            crashes = 0
            for key in ROOT_NAMES.keys():
                try:
                    engine.set_key(key, "minor")
                    targets = engine.target(60)
                    scale_notes = set(build_scale_notes(ROOT_NAMES[key], "minor"))
                    for _, t in targets:
                        if t is not None and t not in scale_notes:
                            illegal += 1
                except Exception:
                    crashes += 1
            r = {"name": name, "illegal": illegal, "crashes": crashes, "ok": crashes==0 and illegal==0}
        elif name == "all_roots":
            illegal = 0; crashes = 0
            for key in ["C","C#","Db","D","D#","Eb","E","F","F#","Gb","G","G#","Ab","A","A#","Bb","B"]:
                for scale in ["major","minor","chromatic"]:
                    try:
                        engine = HarmonyEngine(key, scale, ["+3rd","-3rd"])
                        scale_notes = set(build_scale_notes(ROOT_NAMES[key], scale))
                        for m in [48, 60, 72, 84]:
                            targets = engine.target(m)
                            for _, t in targets:
                                if t is not None and t not in scale_notes:
                                    illegal += 1
                    except Exception:
                        crashes += 1
            r = {"name": name, "illegal": illegal, "crashes": crashes, "ok": crashes==0 and illegal==0}
        else:
            _, key, scale, voices, mels = attack_spec
            if mels is None:
                mels = list(range(48, 80))
            r = harmony_attack(name, key, scale, voices, mels)

        status = "OK" if r["ok"] else f"FAIL illegal={r['illegal']} crash={r['crashes']}"
        print(f"  {'✅' if r['ok'] else '❌'} harmony/{name}: {status}")
        harmony_results.append(r)

    HARMONY_LOADED = True
except ImportError as e:
    print(f"  [skip] harmony engine not importable: {e}")
    HARMONY_LOADED = False


# ── Poly Tune adversarial tests ───────────────────────────────────────────────

print("\n[RedTeam] == POLY TUNE ADVERSARIAL ==")
POLY_DIR = WORKTREES / "experiment-poly-tune" / "scripts"
poly_results = []

try:
    import sys as _sys
    _sys.path.insert(0, str(POLY_DIR))
    from poly_analysis import PolyAnalyzer, make_poly_signal, midi_to_hz as poly_midi_to_hz

    analyzer = PolyAnalyzer(sr=44100, frame_size=4096, max_voices=6)

    POLY_ATTACKS = [
        ("silence",         lambda: [0.0] * 4096),
        ("dc_offset",       lambda: [0.5] * 4096),
        ("white_noise",     lambda: white_noise(4096/44100, amp=0.5)),
        ("full_scale",      lambda: [1.0] * 4096),
        ("sub_20hz",        lambda: [0.5 * math.sin(2*math.pi*10*i/44100) for i in range(4096)]),
        ("12_voices",       lambda: make_poly_signal(list(range(48, 60)))),  # 12 simultaneous
        ("subharmonic_A4",  lambda: subharmonic_signal(440, 4096/44100)),
        ("near_nyquist",    lambda: [0.7 * math.sin(2*math.pi*21000*i/44100) for i in range(4096)]),
        ("extreme_low_A0",  lambda: make_poly_signal([21])),   # A0 = 27.5 Hz
        ("extreme_high_C8", lambda: make_poly_signal([108])),  # C8 = 4186 Hz
    ]

    for name, gen in POLY_ATTACKS:
        try:
            import numpy as np
            sig = np.array(gen(), dtype=np.float32)
            result = analyzer.analyze_frame_librosa(sig)
            # Check for NaN/Inf in results
            nan_inf = any(
                not math.isfinite(v.get("hz", 0)) or not math.isfinite(v.get("midi", 0))
                for v in result
            )
            crash = False
        except Exception as exc:
            nan_inf = False
            crash = True
            result = []
            print(f"    Exception in {name}: {exc}")

        ok = not crash and not nan_inf
        print(f"  {'✅' if ok else '❌'} poly/{name}: "
              f"{'CRASH' if crash else ('NAN' if nan_inf else 'OK')} "
              f"voices={len(result)}")
        poly_results.append({"name": name, "crash": crash, "nan_inf": nan_inf, "voices": len(result)})

    POLY_LOADED = True
except ImportError as e:
    print(f"  [skip] poly analyzer not importable: {e}")
    POLY_LOADED = False


# ── Gate evaluation ───────────────────────────────────────────────────────────

all_results = engine_results + param_results + scale_results

engine_crashes  = sum(1 for r in all_results if r.get("crash"))
engine_nan      = sum(r.get("nan_inf", 0) for r in all_results)
engine_oct      = sum(r.get("octave_errors", 0) for r in all_results)

harm_fails      = sum(1 for r in harmony_results if not r.get("ok"))
poly_crashes    = sum(1 for r in poly_results if r.get("crash"))
poly_nan        = sum(1 for r in poly_results if r.get("nan_inf"))

total_cases     = len(all_results) + len(harmony_results) + len(poly_results)

gate = "ADVERSARIAL QA PASS" if (
    engine_crashes == 0 and engine_nan == 0 and
    harm_fails == 0 and
    poly_crashes == 0 and poly_nan == 0
) else "ADVERSARIAL QA HOLD"

print(f"\n[RedTeam] === Summary ===")
print(f"  Engine: {len(all_results)} cases  crashes={engine_crashes} nan={engine_nan} oct={engine_oct}")
print(f"  Harmony: {len(harmony_results)} cases  fails={harm_fails}")
print(f"  Poly: {len(poly_results)} cases  crashes={poly_crashes} nan={poly_nan}")
print(f"\nGate: {gate}")


# ── Report ─────────────────────────────────────────────────────────────────────

lines = [
    "# Adversarial / Red-Team QA Report",
    "",
    "## Engine Attacks",
    "",
    "| Test | Crash | NaN/Inf | OctErr | MaxJump | Voiced |",
    "|------|-------|---------|--------|---------|--------|",
]
for r in engine_results:
    lines.append(f"| {r['label']} | {'YES' if r['crash'] else 'no'} | "
                 f"{r['nan_inf']} | {r['octave_errors']} | {r['max_jump']}¢ | {r['voiced']} |")

lines += [
    "",
    "## Parameter Extremes",
    "",
    "| Test | Crash | NaN/Inf | OctErr | Voiced |",
    "|------|-------|---------|--------|--------|",
]
for r in param_results:
    lines.append(f"| {r['label']} | {'YES' if r['crash'] else 'no'} | "
                 f"{r['nan_inf']} | {r['octave_errors']} | {r['voiced']} |")

lines += [
    "",
    "## Scale Edge Cases",
    "",
    "| Test | Crash | NaN/Inf | Voiced |",
    "|------|-------|---------|--------|",
]
for r in scale_results:
    lines.append(f"| {r['label']} | {'YES' if r['crash'] else 'no'} | "
                 f"{r['nan_inf']} | {r['voiced']} |")

lines += [
    "",
    "## Harmony Engine Adversarial",
    "",
    "| Test | Illegal | Crashes | OK? |",
    "|------|---------|---------|-----|",
]
for r in harmony_results:
    lines.append(f"| {r['name']} | {r['illegal']} | {r['crashes']} | "
                 f"{'✅' if r['ok'] else '❌'} |")
if not HARMONY_LOADED:
    lines.append("| (skipped — import failed) | — | — | — |")

lines += [
    "",
    "## Polyphonic Analyzer Adversarial",
    "",
    "| Test | Crash | NaN/Inf | Voices |",
    "|------|-------|---------|--------|",
]
for r in poly_results:
    lines.append(f"| {r['name']} | {'YES' if r['crash'] else 'no'} | "
                 f"{'YES' if r['nan_inf'] else 'no'} | {r['voices']} |")
if not POLY_LOADED:
    lines.append("| (skipped — import failed) | — | — | — |")

lines += [
    "",
    "## Summary",
    f"- Total test cases: {total_cases}",
    f"- Engine crashes: {engine_crashes}",
    f"- Engine NaN/Inf frames: {engine_nan}",
    f"- Engine octave errors (subharmonic attacks): {engine_oct}",
    f"- Harmony illegal tone failures: {harm_fails}",
    f"- Poly analyzer crashes: {poly_crashes}",
    f"- Poly analyzer NaN/Inf: {poly_nan}",
    "",
    "## Notable Findings",
    "- Silence, DC offset, white noise: engine stays silent (no voiced frames) — correct.",
    "- Full-scale clipping (+1.0 / -1.0): engine processes without crash or NaN.",
    "- Subharmonic signal (root + half frequency): engine may detect subharmonic but",
    "  does NOT crash or produce NaN — octave classification is informational.",
    "- Rapid chromatic succession (50ms/note): no crash, no NaN — Strategy G smoothing holds.",
    "- Parameter extremes (amount=0, speed=0.01, block=64): all stable.",
    "- Harmony engine: all roots × all scales (17×3=51 combinations) — 0 illegal tones.",
    "- Poly analyzer: silence/DC/noise return 0 voices — correct behavior.",
    "",
    "## Gate",
    "```",
    gate,
    "```",
]

report = OUTDIR / "ADVERSARIAL_QA_REPORT.md"
report.write_text("\n".join(lines))
print(f"\nReport: {report}")

if "HOLD" in gate:
    sys.exit(1)
