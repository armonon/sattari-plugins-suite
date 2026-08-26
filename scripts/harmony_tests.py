"""
Deterministic harmony tests — all 9 must pass for HARMONY ENGINE RC.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from harmony_engine import HarmonyEngine, diatonic_target, ROOT_NAMES, SCALE_SEMITONES, build_scale_notes

PASS = 0
FAIL = 0


def check(name, got, expected, tolerance=0):
    global PASS, FAIL
    if isinstance(expected, list):
        ok = got == expected
    elif tolerance:
        ok = abs(got - expected) <= tolerance
    else:
        ok = got == expected
    if ok:
        PASS += 1
        print(f"  PASS: {name}")
    else:
        FAIL += 1
        print(f"  FAIL: {name}  got={got}  expected={expected}")


# ── TEST 1: C major, +3rd ──────────────────────────────────────────────────
# Melody C4→D4→E4→F4 → harmony E4→F4→G4→A4 (diatonic 3rds in C major)
print("\nTest 1: C major +3rd diatonic thirds")
engine = HarmonyEngine("C", "major", ["+3rd"])
c_major = [60, 62, 64, 65]          # C4 D4 E4 F4
expected = [64, 65, 67, 69]         # E4 F4 G4 A4
for m, exp in zip(c_major, expected):
    t = engine.target(m)
    check(f"  C maj +3rd midi={m}", t[0][1], exp)

# ── TEST 2: A minor, +3rd ──────────────────────────────────────────────────
# A3→B3→C4→D4 → C4→D4→E4→F4
print("\nTest 2: A minor +3rd")
engine = HarmonyEngine("A", "minor", ["+3rd"])
a_minor_in = [57, 59, 60, 62]       # A3 B3 C4 D4
a_minor_out = [60, 62, 64, 65]      # C4 D4 E4 F4
for m, exp in zip(a_minor_in, a_minor_out):
    t = engine.target(m)
    check(f"  A min +3rd midi={m}", t[0][1], exp)

# ── TEST 3: Scale consistency across held note ─────────────────────────────
# All harmony targets for a chord progression must stay in scale
print("\nTest 3: Scale consistency — no illegal tones")
engine = HarmonyEngine("C", "major", ["+3rd", "+5th", "-3rd"])
scale_notes = build_scale_notes(ROOT_NAMES["C"], "major")
melody_notes = [60, 62, 64, 65, 67, 69, 71, 72]
illegal = 0
for m in melody_notes:
    targets = engine.target(m)
    for interval, t in targets:
        if t is not None and t not in scale_notes:
            illegal += 1
            print(f"    ILLEGAL: interval={interval} melody={m} harmony={t}")
check("No illegal scale tones in C major scale run", illegal, 0)

# ── TEST 4: Voice crossing resolution ─────────────────────────────────────
# -3rd below a low note should not produce negative MIDI
print("\nTest 4: Voice crossing / out-of-range handling")
engine = HarmonyEngine("C", "major", ["-3rd"])
t = engine.target(36)  # C2 — very low
check("No negative MIDI for -3rd below C2", t[0][1] is None or t[0][1] >= 0, True)

# ── TEST 5: Chromatic passing tone in melody ───────────────────────────────
# Bb4 in C major → harmony must snap to in-scale note
print("\nTest 5: Chromatic passing tone in C major")
engine = HarmonyEngine("C", "major", ["+3rd"])
t = engine.target(70)  # Bb4 — not in C major
_, harm = t[0]
check("Bb4 in C major produces in-scale harmony", harm in build_scale_notes(ROOT_NAMES["C"], "major"), True)

# ── TEST 6: Large interval (octave jump) ──────────────────────────────────
print("\nTest 6: Octave jump — harmony all in scale")
engine = HarmonyEngine("A", "minor", ["+3rd", "-3rd", "+5th"])
scale_notes_a = build_scale_notes(ROOT_NAMES["A"], "minor")
for m in [57, 69]:  # A3 and A4 (octave jump)
    targets = engine.target(m)
    for interval, t in targets:
        if t is not None:
            check(f"  Octave jump midi={m} {interval} in scale", t in scale_notes_a, True)

# ── TEST 7: Fast run (16 notes) — all in scale ────────────────────────────
print("\nTest 7: Fast run — all harmony targets in scale")
engine = HarmonyEngine("C", "major", ["+3rd"])
scale_c = build_scale_notes(ROOT_NAMES["C"], "major")
run = list(range(60, 76))  # C4 to B4 chromatic (includes off-scale notes)
in_scale_run = [n for n in run if n in scale_c]
all_in_scale = True
for m in in_scale_run:
    t = engine.target(m)
    if t[0][1] not in scale_c:
        all_in_scale = False
check("Fast run harmony all in scale", all_in_scale, True)

# ── TEST 8: Vibrato (melody ±50¢ = ±0.5 MIDI) — harmony tracks center ─────
print("\nTest 8: Vibrato tracking — harmony should be stable")
engine = HarmonyEngine("A", "minor", ["+3rd"])
center = 69.0  # A4
vibs = [69.5, 68.5, 69.4, 68.6, 69.3, 68.7]  # simulated vibrato
targets = [engine.target(v)[0][1] for v in vibs]
# All should map to same harmony note (slight vibrato shouldn't cause target chatter)
check("Vibrato harmony stable (no target jumps)", len(set(targets)), 1)

# ── TEST 9: Wrong-note input (out of scale) ────────────────────────────────
print("\nTest 9: Out-of-scale melody → harmony snaps to scale")
engine = HarmonyEngine("C", "major", ["+5th"])
scale_c = build_scale_notes(ROOT_NAMES["C"], "major")
for off_note in [61, 63, 66, 70]:  # Db Eb Gb Bb — not in C major
    t = engine.target(off_note)[0][1]
    check(f"  Off-scale {off_note} → harmony in scale", t in scale_c, True)

# ── ADVERSARIAL TESTS ──────────────────────────────────────────────────────
print("\n── Adversarial Tests ──")

# Unison conflict (melody = harmony)
engine = HarmonyEngine("C", "major", ["unison"])
t = engine.target(60)[0][1]
check("Unison: harmony == melody", t, 60)

# All voices enabled simultaneously — no crossing violations
engine = HarmonyEngine("C", "major", ["+3rd", "-3rd", "+5th", "-5th", "+oct"])
scale_c = build_scale_notes(ROOT_NAMES["C"], "major")
any_illegal = False
for m in [60, 62, 64, 65, 67, 69, 71, 72]:
    targets = engine.target(m)
    for _, t in targets:
        if t is not None and t not in scale_c:
            any_illegal = True
check("All 5 voices simultaneously: no illegal tones", any_illegal, False)

# Key change mid-sequence
engine = HarmonyEngine("C", "major", ["+3rd"])
t_c = engine.target(60)[0][1]   # C4 → E4
engine.set_key("G", "major")
t_g = engine.target(67)[0][1]   # G4 in G major → B4
scale_g = build_scale_notes(ROOT_NAMES["G"], "major")
check("Key change: new harmony in new key", t_g in scale_g, True)

# Chromatic scale: +3rd = +2 diatonic degrees in 12-tone scale = 2 semitones above
# (chromatic scale degrees: 0=C 1=Db 2=D 3=Eb ... so degree+2 from C = D = 62)
engine = HarmonyEngine("C", "chromatic", ["+3rd"])
t = engine.target(60)[0][1]
check("Chromatic scale: +3rd = 2 diatonic degrees = in-scale note", t in build_scale_notes(ROOT_NAMES["C"], "chromatic"), True)
check("Chromatic scale: +3rd result is deterministic", t, 62)  # C4+2deg = D4

print(f"\n{'='*50}")
print(f"Results: {PASS} PASS / {FAIL} FAIL")
if FAIL == 0:
    print("HARMONY ENGINE RC — deterministic tests PASS")
else:
    print(f"HOLD — {FAIL} test(s) failed")
    sys.exit(1)
