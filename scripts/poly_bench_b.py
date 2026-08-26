"""
Poly Tune Phase B Benchmark
Gate: Precision >= 0.93 AND Recall >= 0.85 → POLY TUNE BETA
"""

import sys, math
import numpy as np
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from poly_analysis_b import PolyAnalyzerB, make_poly_signal

OUT = Path(__file__).parent.parent / "validation" / "poly_tune" / "POLY_BENCH_B_REPORT.md"
OUT.parent.mkdir(parents=True, exist_ok=True)

analyzer = PolyAnalyzerB(sr=44100, n_fft=8192, hop=1024,
                          max_voices=6, min_hz=80.0, max_hz=2000.0,
                          conf_threshold=0.04, cluster_semitones=0.5)

CASES = [
    # 2-note intervals
    ("interval_maj3_C4E4",    [60, 64]),
    ("interval_min3_A3C4",    [57, 60]),
    ("interval_p5_C4G4",      [60, 67]),
    ("interval_tritone_C4F#4",[60, 66]),
    ("interval_oct_C3C4",     [48, 60]),
    ("interval_maj6_C4A4",    [60, 69]),
    ("interval_min7_C4Bb4",   [60, 70]),

    # 3-note chords
    ("chord_Cmaj",            [60, 64, 67]),
    ("chord_Amin",            [57, 60, 64]),
    ("chord_Gmaj",            [55, 59, 62]),
    ("chord_Emin",            [52, 55, 59]),
    ("chord_Fmaj",            [53, 57, 60]),
    ("chord_dom7_G7",         [55, 59, 62, 65]),

    # 4-note chords
    ("chord_Cmaj7",           [60, 64, 67, 71]),
    ("chord_Amin7",           [57, 60, 64, 67]),
    ("chord_dom7_C7",         [60, 64, 67, 70]),
    ("chord_half_dim_B",      [59, 62, 65, 69]),

    # Guitar power chord
    ("power_A2A3E3",          [45, 52, 57]),
    ("power_E2B2E3",          [40, 47, 52]),

    # String pad (close voicing)
    ("pad_Cmaj_close",        [60, 64, 67]),
    ("pad_Amin_close",        [57, 60, 64]),

    # Piano open voicing
    ("piano_Cmaj_open",       [48, 64, 67]),
    ("piano_Gmaj_open",       [43, 59, 62]),

    # Monophonic baseline
    ("mono_A4",               [69]),
    ("mono_C4",               [60]),
    ("mono_E3",               [52]),

    # Wide-spaced chords (harder)
    ("chord_Dmin7_wide",      [50, 57, 62, 69]),
    ("chord_Cmaj_wide",       [48, 60, 67]),

    # Close mid-range intervals (hardest for HPS)
    ("interval_p5_mid_E4B4",  [64, 71]),
    ("interval_maj3_mid_G4B4",[67, 71]),
    ("interval_min3_mid_A4C5",[69, 72]),
]

CENTS_TOL = 30


def evaluate(gt_midi, detected):
    gt_set = list(gt_midi)
    det_midis = [int(round(d["midi"])) for d in detected]

    matched_gt = set()
    matched_det = set()
    cents_errors = []

    for di, dm in enumerate(det_midis):
        best, best_dist = None, 999
        for gi, gm in enumerate(gt_set):
            dist = abs(dm - gm)
            if dist <= 1 and dist < best_dist and gi not in matched_gt:
                best, best_dist = gi, dist
        if best is not None:
            matched_gt.add(best)
            matched_det.add(di)
            cents_errors.append(abs(detected[di]["midi"] - gt_set[best]) * 100)

    tp = len(matched_gt)
    fp = len(det_midis) - len(matched_det)
    fn = len(gt_set) - len(matched_gt)
    avg_ce = sum(cents_errors) / len(cents_errors) if cents_errors else 0
    return tp, fp, fn, round(avg_ce, 1)


print("[PolyBenchB] Running Phase B ground truth evaluation...")
results = []
total_tp = total_fp = total_fn = 0
chord_correct = 0

for label, gt_midi in CASES:
    audio = make_poly_signal(gt_midi).astype(np.float32)
    detected = analyzer.analyze(audio)
    tp, fp, fn, ce = evaluate(gt_midi, detected)
    total_tp += tp
    total_fp += fp
    total_fn += fn
    chord_ok = (fn == 0 and fp == 0)
    if chord_ok:
        chord_correct += 1
    status = "OK" if chord_ok else ("P" if tp > 0 else "MISS")
    det_ints = [d["midi_int"] for d in detected]
    print(f"  [{status}] {label}: gt={gt_midi} det={det_ints} "
          f"tp={tp} fp={fp} fn={fn} ce={ce}¢")
    results.append(dict(label=label, gt=gt_midi, n_gt=len(gt_midi), n_det=len(detected),
                         tp=tp, fp=fp, fn=fn, ce=ce, chord_ok=chord_ok))

precision = total_tp / max(total_tp + total_fp, 1)
recall    = total_tp / max(total_tp + total_fn, 1)
chord_acc = chord_correct / len(CASES)

print(f"\n[PolyBenchB] Precision={precision:.3f}  Recall={recall:.3f}  "
      f"Chord Acc={chord_acc:.3f}  ({chord_correct}/{len(CASES)} chords exact)")

gate_pass = precision >= 0.93 and recall >= 0.85
if gate_pass:
    gate = "POLY TUNE BETA"
else:
    blockers = []
    if precision < 0.93:
        blockers.append(f"Precision={precision:.3f} < 0.93 (delta={0.93-precision:.3f})")
    if recall < 0.85:
        blockers.append(f"Recall={recall:.3f} < 0.85 (delta={0.85-recall:.3f})")
    gate = "POLY TUNE BETA HOLD — " + "; ".join(blockers)

print(f"\nGate: {gate}")

lines = [
    "# Polyphonic Analysis — Phase B Report",
    "",
    "**Method:** Spectral Salience (librosa.salience) + harmonic suppression",
    f"**n_fft:** {analyzer.n_fft}  **SR:** {analyzer.sr}  **Max voices:** {analyzer.max_voices}",
    f"**conf_threshold:** {analyzer.conf_threshold}  **suppress_ratio:** 0.40",
    "",
    "## Results",
    "",
    "| Case | GT Notes | Detected | TP | FP | FN | Cents Err |",
    "|------|----------|----------|----|----|----|-----------|",
]
for r in results:
    det_label = f"{r['n_det']}/{r['n_gt']}"
    status_sym = "✅" if r['chord_ok'] else ("⚠" if r['tp'] > 0 else "❌")
    lines.append(f"| {status_sym} {r['label']} | {r['gt']} | {det_label} | "
                 f"{r['tp']} | {r['fp']} | {r['fn']} | {r['ce']}¢ |")

lines += [
    "",
    "## Summary",
    f"- **Precision:** {precision:.3f} ({total_tp}/{total_tp+total_fp}) "
    f"{'✅' if precision >= 0.93 else '❌'} (gate ≥0.93)",
    f"- **Recall:** {recall:.3f} ({total_tp}/{total_tp+total_fn}) "
    f"{'✅' if recall >= 0.85 else '❌'} (gate ≥0.85)",
    f"- **Chord accuracy:** {chord_acc:.3f} ({chord_correct}/{len(CASES)} exact)",
    "",
    "## Gate",
    "```",
    gate,
    "```",
]
OUT.write_text("\n".join(lines))
print(f"Report: {OUT}")

if not gate_pass:
    sys.exit(1)
