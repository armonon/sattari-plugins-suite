"""
Poly Tune Phase A Benchmark — ground truth synthetic corpus.
Measures precision, recall, chord accuracy, cents error.
"""

import sys, math
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from poly_analysis import PolyAnalyzer, make_poly_signal, hz_to_midi, midi_to_hz

OUT = Path(__file__).parent.parent / "validation" / "poly_tune" / "POLY_ANALYSIS_REPORT.md"
OUT.parent.mkdir(parents=True, exist_ok=True)

analyzer = PolyAnalyzer(sr=44100, frame_size=4096, max_voices=6,
                         min_hz=80.0, max_hz=2000.0, conf_threshold=0.20)

# Ground truth test cases: (label, midi_notes)
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

    # Guitar power chord (root + 5th + octave)
    ("power_A2A3E3",          [45, 52, 57]),
    ("power_E2B2E3",          [40, 47, 52]),

    # String pad (root + 3rd + 5th with close voicing)
    ("pad_Cmaj_close",        [60, 64, 67]),
    ("pad_Amin_close",        [57, 60, 64]),

    # Piano open voicing
    ("piano_Cmaj_open",       [48, 64, 67]),   # C3 E4 G4
    ("piano_Gmaj_open",       [43, 59, 62]),   # G2 B3 D4

    # 1-note baseline (monophonic)
    ("mono_A4",               [69]),
    ("mono_C4",               [60]),
    ("mono_E3",               [52]),

    # Harder: 3-note minor 7th with wide spacing
    ("chord_Dmin7_wide",      [50, 57, 62, 69]),   # D3 A3 D4 A4
    ("chord_Cmaj_wide",       [48, 60, 67]),        # C3 C4 G4

    # Mid-range 2-note (harder for HPS — close harmonics overlap)
    ("interval_p5_mid_E4B4",  [64, 71]),
    ("interval_maj3_mid_G4B4",[67, 71]),
    ("interval_min3_mid_A4C5",[69, 72]),
]

CENTS_TOLERANCE = 30  # cents

def evaluate(gt_midi, detected):
    """
    Match detected notes to ground truth.
    Returns: tp, fp, fn, avg_cents_error
    """
    gt_set = list(gt_midi)
    det_midis = [int(round(d["midi"])) for d in detected]

    matched_gt = set()
    matched_det = set()
    cents_errors = []

    for di, dm in enumerate(det_midis):
        best = None
        best_dist = 999
        for gi, gm in enumerate(gt_set):
            dist = abs(dm - gm)
            if dist <= 1 and dist < best_dist and gi not in matched_gt:
                best = gi
                best_dist = dist
        if best is not None:
            matched_gt.add(best)
            matched_det.add(di)
            cents_err = abs(detected[di]["midi"] - gt_set[best]) * 100
            cents_errors.append(cents_err)

    tp = len(matched_gt)
    fp = len(det_midis) - len(matched_det)
    fn = len(gt_set) - len(matched_gt)
    avg_ce = sum(cents_errors) / len(cents_errors) if cents_errors else 0
    return tp, fp, fn, round(avg_ce, 1)


print("[PolyBench] Running ground truth evaluation...")
results = []
total_tp = total_fp = total_fn = 0
chord_correct = 0

for label, gt_midi in CASES:
    audio = make_poly_signal(gt_midi)
    detected = analyzer.analyze_frame_librosa(audio[:analyzer.frame_size])
    tp, fp, fn, ce = evaluate(gt_midi, detected)
    total_tp += tp
    total_fp += fp
    total_fn += fn
    chord_ok = (fn == 0 and fp == 0)
    if chord_ok:
        chord_correct += 1
    n_notes = len(gt_midi)
    n_det   = len(detected)
    status = "✅" if chord_ok else ("⚠" if tp > 0 else "❌")
    print(f"  {status} {label}: gt={gt_midi} det={[d['midi_int'] for d in detected]} "
          f"tp={tp} fp={fp} fn={fn} ce={ce}¢")
    results.append(dict(label=label, gt=gt_midi, n_gt=n_notes, n_det=n_det,
                         tp=tp, fp=fp, fn=fn, ce=ce, chord_ok=chord_ok))

precision = total_tp / max(total_tp + total_fp, 1)
recall    = total_tp / max(total_tp + total_fn, 1)
chord_acc = chord_correct / len(CASES)

print(f"\n[PolyBench] Precision={precision:.3f}  Recall={recall:.3f}  "
      f"Chord Acc={chord_acc:.3f}  ({chord_correct}/{len(CASES)} chords exact)")

gate = "POLY TUNE ALPHA PASS" if precision >= 0.70 and recall >= 0.65 else "POLY TUNE ALPHA HOLD"
print(f"\nGate: {gate}")

# Write report
lines = [
    "# Polyphonic Analysis — Phase A Report",
    "",
    "**Method:** Iterative HPS (Harmonic Product Spectrum) with harmonic subtraction",
    f"**Frame size:** 4096  **SR:** 44100  **Max voices:** 6",
    "",
    "## Results",
    "",
    "| Case | GT Notes | Detected | TP | FP | FN | Cents Err |",
    "|------|----------|----------|----|----|----|-----------|",
]
for r in results:
    det_label = f"{r['n_det']}/{r['n_gt']}"
    lines.append(f"| {r['label']} | {r['gt']} | {det_label} | {r['tp']} | {r['fp']} | {r['fn']} | {r['ce']}¢ |")

lines += [
    "",
    "## Summary",
    f"- **Precision:** {precision:.3f} ({total_tp}/{total_tp+total_fp})",
    f"- **Recall:** {recall:.3f} ({total_tp}/{total_tp+total_fn})",
    f"- **Chord accuracy:** {chord_acc:.3f} ({chord_correct}/{len(CASES)} exact)",
    "",
    "## Known Limitations",
    "- HPS struggles with close intervals (minor 2nd, major 2nd) due to spectral overlap",
    "- Wide-voicing chords (>2 octaves) can fool HPS into partial F0 detection",
    "- Distorted/noisy signals will reduce precision significantly",
    "- This is Phase A only — Phase B should add note tracking and confidence smoothing",
    "",
    "## Phase B Recommendation",
    "- Add temporal note tracking (Kalman or median filter over multiple frames)",
    "- Use CQT-based analysis for better low-frequency resolution on bass notes",
    "- Minimum note duration gate (50ms) to suppress phantom detections",
    "",
    "## Gate",
    "```",
    gate,
    "```",
]
OUT.write_text("\n".join(lines))
print(f"Report: {OUT}")

if "PASS" not in gate:
    sys.exit(1)
