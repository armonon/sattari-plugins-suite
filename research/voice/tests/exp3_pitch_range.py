"""EXPERIMENT 3 — how far can the instrument be transposed before it stops
sounding like the voice?

WHY THIS IS NOT A GUESS
-----------------------
A sampler answers this with "a few semitones either way, then add another
sample". This instrument's answer should come from where its own error crosses
audibility, and that number is different going up than going down because the
two directions fail for OPPOSITE reasons:

  DOWN  the harmonics get denser than the ones that were measured. Every odd new
        harmonic lands between two observed ones, so the limit is how well the
        envelope INTERPOLATES.

  UP    the harmonics get sparser and march off the top of the measured range.
        The limit is the reliable ceiling — beyond it the model is inventing
        brightness, and the render deliberately rolls off instead.

So the usable range should be ASYMMETRIC, and it should depend on the source
pitch: a bass note carries far more evidence about the throat than a soprano
note does.

MEASURED AGAINST TRUTH, NOT AGAINST A RE-ESTIMATE
-------------------------------------------------
The rendered note's harmonics are compared to the TRUE envelope evaluated at
those same harmonic frequencies. The tempting alternative — re-analyse the
render and compare envelopes — quietly measures the wrong thing at the extremes:
at +24 semitones a render has a handful of harmonics, so the ESTIMATOR degrades
too, and the experiment would report synthesis error that is really measurement
error. Here f0 is known exactly (it is what we asked for), so harmonic
amplitudes are read directly and the truth curve is exact at every frequency.

    python tests/exp3_pitch_range.py
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from analysis import extract_harmonics_multi  # noqa: E402
from model import PitchTrack  # noqa: E402
from pipeline import analyse_voice  # noqa: E402
from synthesis import render, render_by_resampling  # noqa: E402
from synth import synth_vowel  # noqa: E402

SR = 48000
# SCORED TO 8 kHz, ABOVE EVERY SOURCE'S RELIABLE CEILING (6.4-7.7 kHz here).
# Capping at 5 kHz kept the whole sweep inside measured territory and made the
# up direction look free — the hypothesised failure, harmonics marching past the
# ceiling into invented brightness, was simply never exercised. Scoring past it
# is the only way the roll-off policy gets tested.
SCORE_TO_HZ = 8000.0
failures = 0


def check(ok: bool, message: str) -> None:
    global failures
    if not ok:
        print(f"  FAIL {message}")
        failures += 1


def spectral_error(audio, f0_hz, truth, sr=SR):
    """Mean |dB| between the rendered harmonics and the true throat.

    f0 is KNOWN (we asked for this note), so no pitch tracking is involved and
    no estimator can degrade at the extremes of the sweep.
    """
    duration = audio.size / sr
    if duration < 0.5 or f0_hz <= 0:
        return float("nan"), 0
    pitch = PitchTrack(times_s=[0.0, duration], frequency_hz=[f0_hz, f0_hz])

    a_s, b_s = 0.25 * duration, 0.85 * duration      # steady middle only
    h = extract_harmonics_multi(audio, sr, pitch, a_s, b_s)
    if h.n_harmonics < 3:
        return float("nan"), h.n_harmonics

    hz = np.asarray(h.frequency_hz)
    db = np.asarray(h.amplitude_db)
    keep = (hz > 0) & (hz <= SCORE_TO_HZ)
    if np.count_nonzero(keep) < 3:
        return float("nan"), int(np.count_nonzero(keep))

    hz, db = hz[keep], db[keep]
    true_db = truth["envelope_db_at"](hz)
    # One constant offset: the model is level-independent by design.
    db = db + float(np.median(true_db - db))
    return float(np.mean(np.abs(db - true_db))), hz.size


def sweep(source_f0, vowel, label):
    audio, truth = synth_vowel(f0_hz=source_f0, vowel=vowel, duration_s=3.0,
                               sample_rate=SR, breath_db=-30.0)
    model = analyse_voice(audio, SR, label)
    ceiling = model.harmonics.reliable_up_to_hz

    print(f"\n  SOURCE {label}: f0 {source_f0:.0f} Hz '{vowel}', "
          f"{model.harmonics.n_harmonics} harmonics, reliable to {ceiling:.0f} Hz")
    print(f"    {'interval':>9}{'played':>9}{'model':>9}{'resampled':>11}"
          f"{'harms':>8}{'invented':>10}")

    rows = []
    for semitones in range(-24, 25, 3):
        played = source_f0 * 2.0 ** (semitones / 12.0)
        if played < 45.0 or played > 1400.0:
            continue

        rendered = render(model, played, duration_s=1.5, sr=SR, apply_noise=False)
        model_err, n = spectral_error(rendered, played, truth)

        control = render_by_resampling(audio, float(semitones))
        control_err, _ = spectral_error(control, played, truth)

        above = int(np.sum(np.arange(1, n + 1) * played > ceiling)) if n else 0
        rows.append((semitones, played, model_err, control_err, n, above))
        print(f"    {semitones:>+9d}{played:>9.0f}{model_err:>8.2f} dB"
              f"{control_err:>10.2f} dB{n:>8d}{above:>10d}")

    return rows, model


def usable_span(rows, threshold_db):
    """Widest unbroken run of intervals around unison that stays under the bar."""
    ok = {r[0] for r in rows if np.isfinite(r[2]) and r[2] < threshold_db}
    if 0 not in ok:
        return 0, 0
    lo = hi = 0
    while lo - 3 in ok:
        lo -= 3
    while hi + 3 in ok:
        hi += 3
    return lo, hi


def main() -> int:
    print("EXPERIMENT 3 — transposition range")
    print("=" * 68)
    print("Spectral error of the rendered note against the true throat.")
    print("'resampled' is the same interval done the chipmunk way, for scale.")

    sources = [
        (110.0, "ah", "bass"),
        (220.0, "ah", "alto"),
        (440.0, "oo", "soprano"),
    ]
    results = {}
    for f0, vowel, label in sources:
        results[label] = sweep(f0, vowel, label)

    # 3 dB is where a broadband spectral difference starts being audible as a
    # timbre change; 6 dB is where it reads as a different instrument. These are
    # CONVENTIONS, not thresholds measured on listeners — the honest use of them
    # is comparative (model vs control), not absolute.
    print("\n" + "=" * 68)
    print("USABLE RANGE  (semitones from the recorded note)")
    print(f"    {'source':<10}{'< 3 dB (same voice)':>24}{'< 6 dB (still musical)':>26}")
    for label, (rows, _) in results.items():
        lo3, hi3 = usable_span(rows, 3.0)
        lo6, hi6 = usable_span(rows, 6.0)
        print(f"    {label:<10}{f'{lo3:+d} to {hi3:+d}':>24}{f'{lo6:+d} to {hi6:+d}':>26}")

    print("\n    model error vs the resampling control, averaged over the sweep")
    for label, (rows, _) in results.items():
        m = [r[2] for r in rows if np.isfinite(r[2])]
        c = [r[3] for r in rows if np.isfinite(r[3])]
        ratio = (np.mean(m) / np.mean(c)) if c and np.mean(c) > 0 else float("nan")
        print(f"    {label:<10}model {np.mean(m):5.2f} dB   "
              f"control {np.mean(c):5.2f} dB   model is {ratio * 100:.0f}% of control")
        check(ratio < 0.5, f"{label}: model is not clearly better than resampling")

    # --- the asymmetry, stated as a number -------------------------------
    print("\n    DOWN vs UP  (mean error either side of unison)")
    for label, (rows, _) in results.items():
        down = [r[2] for r in rows if r[0] < 0 and np.isfinite(r[2])]
        up = [r[2] for r in rows if r[0] > 0 and np.isfinite(r[2])]
        if down and up:
            print(f"    {label:<10}down {np.mean(down):5.2f} dB    up {np.mean(up):5.2f} dB")

    print("\n    CAVEAT: the scored harmonic count varies across each sweep (45 down")
    print("    to 4 for the soprano), so a note with four partials is judged on four.")
    print("    That is the right perceptual question but makes the columns only")
    print("    loosely comparable across intervals — read each row, not the trend.")

    for label, (rows, _) in results.items():
        lo3, hi3 = usable_span(rows, 3.0)
        check(hi3 - lo3 >= 6, f"{label}: usable range is under an octave ({lo3:+d}..{hi3:+d})")
        unison = [r[2] for r in rows if r[0] == 0]
        # Gated at the same-voice threshold, NOT at some absolute polish bar. A
        # sparse source cannot reproduce even itself to a fraction of a dB — the
        # soprano has 11 harmonics to define a whole throat — and demanding that
        # would be asserting a quality the evidence does not contain.
        check(bool(unison) and unison[0] < 3.0,
              f"{label}: unison is already a different voice "
              f"({unison[0] if unison else float('nan'):.2f} dB)")
        worst = max((r[2] for r in rows if np.isfinite(r[2])), default=float("inf"))
        check(worst < 6.0, f"{label}: error blows up somewhere in the sweep ({worst:.2f} dB)")

    print("\n" + "=" * 68)
    if failures == 0:
        print("EXPERIMENT 3 PASSED — the playable range is measured, not assumed.")
        return 0
    print(f"EXPERIMENT 3 FAILED ({failures})")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
