"""
Harmony Engine Performance Benchmark — 600 melody frames, voice count scaling.
"""
import sys, time, math, random
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from harmony_engine import HarmonyEngine, build_scale_notes, ROOT_NAMES

RNG = random.Random(7)
OUT = Path(__file__).parent.parent / "validation" / "harmony_engine" / "HARMONY_BENCH_REPORT.md"
OUT.parent.mkdir(parents=True, exist_ok=True)

SCALES_KEYS = [("C","major"),("A","minor"),("G","major"),("E","minor"),("F","major"),("D","minor")]


def gen_melody(key, scale, n=600):
    """Generate a realistic melody: mix of step motion, leaps, vibrato."""
    scale_notes = build_scale_notes(ROOT_NAMES[key], scale)
    if not scale_notes:
        return [60]*n
    mid_notes = [m for m in scale_notes if 55 <= m <= 75]
    if not mid_notes:
        mid_notes = scale_notes[:10]
    melody = [RNG.choice(mid_notes)]
    for _ in range(n - 1):
        r = RNG.random()
        if r < 0.5:   # step
            cur = melody[-1]
            idx = mid_notes.index(min(mid_notes, key=lambda m: abs(m-cur)))
            d = RNG.choice([-1, 1])
            new_idx = max(0, min(len(mid_notes)-1, idx+d))
            melody.append(float(mid_notes[new_idx]) + (RNG.random()-0.5)*0.8)  # sub-cent vibrato
        elif r < 0.75:  # leap
            melody.append(float(RNG.choice(mid_notes)))
        else:  # repeat
            melody.append(melody[-1])
    return melody


def run_bench(voice_list, key="A", scale="minor", n=600):
    engine = HarmonyEngine(key, scale, voice_list)
    scale_notes = set(build_scale_notes(ROOT_NAMES[key], scale))
    melody = gen_melody(key, scale, n)

    t0 = time.perf_counter()
    all_targets = []
    for m in melody:
        targets = engine.target(m)
        all_targets.append(targets)
    elapsed = time.perf_counter() - t0

    # Metrics
    illegal = 0
    # Chatter: harmony changes when melody is UNCHANGED (sustained note oscillation)
    sustained_chatter = 0
    sustained_frames = 0
    prev_targets = {}
    prev_melody = None
    for i, (m, frame_targets) in enumerate(zip(melody, all_targets)):
        melody_stable = (prev_melody is not None and int(round(m)) == int(round(prev_melody)))
        for interval, t in frame_targets:
            if t is None:
                continue
            if t not in scale_notes:
                illegal += 1
            if melody_stable:
                sustained_frames += 1
                if interval in prev_targets and prev_targets[interval] != t:
                    sustained_chatter += 1
            prev_targets[interval] = t
        prev_melody = m

    n_voice_frames = n * len(voice_list)
    # Chatter only counts oscillations on sustained melody notes
    chatter_pct = 100 * sustained_chatter / max(sustained_frames, 1)
    accuracy_pct = 100 * (1 - illegal / max(n_voice_frames, 1))
    fps = n / elapsed

    return {
        "voices": len(voice_list),
        "frames": n,
        "illegal": illegal,
        "chatter_pct": round(chatter_pct, 2),
        "accuracy_pct": round(accuracy_pct, 2),
        "fps": round(fps),
        "elapsed_ms": round(elapsed * 1000, 1),
    }


VOICE_CONFIGS = [
    (["+3rd"], "1-voice +3rd"),
    (["+3rd", "-3rd"], "2-voice ±3rd"),
    (["+3rd", "+5th", "-3rd", "-5th"], "4-voice ±3rd ±5th"),
    (["+3rd", "+5th", "-3rd", "-5th", "+oct", "-oct"], "6-voice full"),
]

print("[HarmonyBench] Running...")
results = []
for voices, label in VOICE_CONFIGS:
    r = run_bench(voices)
    r["label"] = label
    results.append(r)
    print(f"[HarmonyBench] {label}: accuracy={r['accuracy_pct']}% chatter={r['chatter_pct']}% {r['fps']} fps")

# Cross-key consistency
print("[HarmonyBench] Cross-key sweep...")
cross_results = []
for key, scale in SCALES_KEYS:
    r = run_bench(["+3rd", "+5th", "-3rd"], key=key, scale=scale)
    r["key"] = key; r["scale"] = scale
    cross_results.append(r)
    print(f"[HarmonyBench] {key} {scale}: accuracy={r['accuracy_pct']}% illegal={r['illegal']}")

all_illegal = sum(r["illegal"] for r in results + cross_results)
all_accurate = all(r["accuracy_pct"] >= 95.0 for r in results)
all_chatter_ok = all(r["chatter_pct"] <= 30.0 for r in results)

gate = "HARMONY ENGINE RC" if all_illegal == 0 and all_accurate else "HOLD"

lines = [
    "# Harmony Engine — Bench Report",
    "",
    "## Voice Count Scaling",
    "",
    "| Config | Frames | Illegal | Chatter% | Accuracy% | Throughput |",
    "|--------|--------|---------|----------|-----------|------------|",
]
for r in results:
    lines.append(f"| {r['label']} | {r['frames']} | {r['illegal']} | {r['chatter_pct']}% | {r['accuracy_pct']}% | {r['fps']} fps |")

lines += [
    "",
    "## Cross-Key Consistency",
    "",
    "| Key | Scale | Illegal | Accuracy% |",
    "|-----|-------|---------|-----------|",
]
for r in cross_results:
    lines.append(f"| {r['key']} | {r['scale']} | {r['illegal']} | {r['accuracy_pct']}% |")

lines += [
    "",
    "## Summary",
    f"- Total illegal scale tones: {all_illegal}",
    f"- All configs ≥ 95% accuracy: {all_accurate}",
    f"- Chatter acceptable: {all_chatter_ok}",
    "",
    "## Gate",
    "```",
    gate,
    "```",
]
OUT.write_text("\n".join(lines))
print(f"\n[HarmonyBench] illegal={all_illegal}  accurate={all_accurate}")
print(f"Gate: {gate}")

if gate != "HARMONY ENGINE RC":
    sys.exit(1)
