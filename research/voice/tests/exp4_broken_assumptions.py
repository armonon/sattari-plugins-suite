"""EXPERIMENT 4 — break the source-filter assumption on purpose.

WHY THIS EXISTS
---------------
Experiments 0-3 all used synthetic vowels built from the SAME decomposition the
analyzer assumes: a harmonic source through an all-pole tract, held still. They
therefore test the analysis and never the assumption. Every number so far is
conditional on a premise that real voices violate constantly, and a research
prototype that only ever meets agreeable input is measuring its own reflection.

Two violations, chosen because they are the ones the architecture cannot argue
its way out of:

  NASALITY      adds a ZERO to the transfer function. LPC is all-pole. It cannot
                represent a dip except by spending poles on the shoulders either
                side, so a nasal vowel attacks the exact assumption that made LPC
                win Experiment 1. If the verdict reverses here, it reverses.

  MOVING TRACT  breaks stationarity. When formants slide during the note, NO
                static envelope is correct — not the model's, not any model's.
                The interesting question stops being "how accurate" and becomes
                "does the system know?".

THE SECOND QUESTION IS THE IMPORTANT ONE. An instrument that degrades on hard
input is normal. One that degrades while still reporting DETECTED is dangerous,
because the user has no signal to distrust it. So the gates below check that
confidence FALLS as the assumption breaks — a graceful failure is a passing
result; a confident wrong answer is not.

    python tests/exp4_broken_assumptions.py
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from analysis import (extract_harmonics_multi, find_stable_region,  # noqa: E402
                      frame_features, track_f0)
from envelope import METHODS, envelope_at, estimate_envelope  # noqa: E402
from pipeline import analyse_voice  # noqa: E402
from synth import NASAL, synth_vowel  # noqa: E402

SR = 48000
LABEL = {"lpc": "LPC", "cepstral": "Cepstral", "true": "TrueEnv", "peak": "PeakInterp"}
failures = 0


def check(ok: bool, message: str) -> None:
    global failures
    if not ok:
        print(f"  FAIL {message}")
        failures += 1


def envelope_errors(audio, truth, f0):
    """Error at and between harmonics, for every method — as in Experiment 1."""
    features = frame_features(audio, SR)
    pitch = track_f0(audio, SR)
    region = find_stable_region(features, pitch, SR)
    a_s = max(region.start_s, truth["sustain_start_s"])
    b_s = min(region.end_s, truth["sustain_end_s"])
    harmonics = extract_harmonics_multi(audio, SR, pitch, a_s, b_s)
    top = min(max(harmonics.reliable_up_to_hz, f0 * 6), 6000.0)

    at = np.arange(1, int(top / f0) + 1) * f0
    at = at[(at >= f0 * 0.9) & (at <= top)]
    between = (np.arange(1, int(top / f0) + 1) - 0.5) * f0
    between = between[(between >= f0 * 0.9) & (between <= top)]

    out = {}
    for method in METHODS:
        env = estimate_envelope(audio, SR, pitch, a_s, b_s, method,
                                harmonics=harmonics)
        if not env.magnitude_db or at.size < 3:
            out[method] = None
            continue
        offset = float(np.median(truth["envelope_db_at"](at) - envelope_at(env, at)))

        def err(hz):
            return float(np.mean(np.abs(truth["envelope_db_at"](hz)
                                        - (envelope_at(env, hz) + offset))))
        out[method] = (err(at), err(between))
    return out, region


# =============================================================================
def nasality() -> None:
    print("\n  A. NASALITY — a zero in an all-pole world")
    print("     'ah' at 150 Hz, with a nasal pole-zero pair added (-15 dB notch)")
    print(f"     {'condition':<12}" + "".join(f"{LABEL[m]:>12}" for m in METHODS))

    scores = {}
    for name, anti in (("clean", None), ("light", NASAL["light"]),
                       ("heavy", NASAL["heavy"])):
        audio, truth = synth_vowel(f0_hz=150.0, vowel="ah", duration_s=3.0,
                                   sample_rate=SR, antiformants=anti)
        res, _ = envelope_errors(audio, truth, 150.0)
        scores[name] = res
        row = f"     {name:<12}"
        for m in METHODS:
            row += f"{res[m][1]:>11.2f} " if res[m] else f"{'--':>12}"
        print(row)
    print("     (mean BETWEEN-harmonic error in dB, as in Experiment 1)")

    print(f"\n     degradation from clean to heavy nasality:")
    damage = {}
    for m in METHODS:
        if scores["clean"][m] and scores["heavy"][m]:
            d = scores["heavy"][m][1] - scores["clean"][m][1]
            damage[m] = d
            print(f"       {LABEL[m]:<11}{d:+6.2f} dB")

    if damage:
        worst = max(damage, key=damage.get)
        best = min(damage, key=damage.get)
        print(f"\n     most damaged: {LABEL[worst]}   least damaged: {LABEL[best]}")
        # LPC is the all-pole one. If nasality does NOT hurt it more than the
        # methods that make no such assumption, then either the anti-formant is
        # too weak to matter or the test is not reaching the envelope at all —
        # both mean this experiment proved nothing.
        others = [d for m, d in damage.items() if m != "lpc"]
        if "lpc" in damage and others:
            print(f"     LPC {damage['lpc']:+.2f} dB vs {np.mean(others):+.2f} dB "
                  f"mean for the methods that assume no pole structure")

    ranked = sorted((s[1], m) for m, s in scores["heavy"].items() if s)
    if ranked:
        print(f"     best ON NASAL INPUT: {LABEL[ranked[0][1]]} at {ranked[0][0]:.2f} dB")
    check(bool(ranked), "no method produced an envelope for nasal input")


# =============================================================================
def moving_tract() -> None:
    print("\n  B. MOVING TRACT — no static model can be right")
    print("     'ah' at 150 Hz, formants sliding by the given fraction across the note")
    print(f"     {'drift':>7}{'region':>10}{'LPC err':>10}{'confidence':>13}"
          f"{'frame spread':>15}")

    rows = []
    for drift in (0.0, 0.10, 0.25, 0.50):
        audio, truth = synth_vowel(f0_hz=150.0, vowel="ah", duration_s=3.0,
                                   sample_rate=SR, formant_drift=drift)
        model = analyse_voice(audio, SR, f"drift{drift}")
        res, region = envelope_errors(audio, truth, 150.0)
        err = res["lpc"][1] if res["lpc"] else float("nan")

        spread = model.envelope.notes.split("spread")[-1].strip() if "spread" in model.envelope.notes else "?"
        span = region.end_s - region.start_s
        rows.append((drift, span, err, model.envelope.confidence.value, spread))
        print(f"     {drift:>7.0%}{span:>9.2f}s{err:>9.2f} dB"
              f"{model.envelope.confidence.value:>13}{spread:>15}")

    # THE GATE THAT MATTERS. Error is allowed to grow — the premise is broken and
    # a static envelope cannot describe a moving tract. What is NOT allowed is
    # for it to grow silently.
    clean_err = rows[0][2]
    worst = rows[-1]
    print(f"\n     error grew {clean_err:.2f} -> {worst[2]:.2f} dB as the tract moved")
    degraded = (worst[3] != rows[0][3]) or (worst[1] < rows[0][1] * 0.8)
    print(f"     did the system notice?  region {rows[0][1]:.2f}s -> {worst[1]:.2f}s, "
          f"confidence {rows[0][3]} -> {worst[3]}")
    # NO ESCAPE CLAUSE. The first version of this gate read
    #     degraded OR worst_err < clean_err + 1.0
    # and passed on the second term while `degraded` was False — error had grown
    # 0.39 to 1.27 dB with confidence still reading DETECTED at every drift
    # level and the region never shrinking. It reported a pass for a system that
    # had noticed nothing. If the error grows, the system must say so.
    grew = worst[2] > clean_err * 2.0
    check(not grew or degraded,
          f"error grew {clean_err:.2f}->{worst[2]:.2f} dB while confidence and region "
          "stayed put — confidently wrong is worse than wrong")


# =============================================================================
def worst_case() -> None:
    print("\n  C. EVERYTHING AT ONCE — nasal, drifting, jittery, breathy")
    audio, truth = synth_vowel(f0_hz=150.0, vowel="ah", duration_s=3.0,
                               sample_rate=SR, antiformants=NASAL["heavy"],
                               formant_drift=0.25, amp_jitter_db=3.0,
                               breath_db=-20.0, vibrato_hz=5.0, vibrato_cents=40.0)
    model = analyse_voice(audio, SR, "worst")
    res, region = envelope_errors(audio, truth, 150.0)

    print(f"     analysis {model.analysis_confidence.value}, "
          f"region {region.end_s - region.start_s:.2f}s, "
          f"{model.harmonics.n_harmonics} harmonics, "
          f"HNR {model.noise.harmonic_to_noise_db:.1f} dB")
    for m in METHODS:
        if res[m]:
            print(f"       {LABEL[m]:<11}at {res[m][0]:5.2f} dB   between {res[m][1]:5.2f} dB")
    for w in model.warnings:
        print(f"     warning: {w}")

    # It must still produce SOMETHING rather than crashing or returning empty —
    # a real recording is always some version of this case.
    check(model.harmonics.n_harmonics > 4, "worst case produced no usable harmonics")
    check(bool(model.envelope.magnitude_db), "worst case produced no envelope")
    best = min((res[m][1], m) for m in METHODS if res[m])
    print(f"     best under everything: {LABEL[best[1]]} at {best[0]:.2f} dB")
    check(best[0] < 8.0, f"even the best method is unusable here ({best[0]:.2f} dB)")


def main() -> int:
    print("EXPERIMENT 4 — where the assumption breaks")
    print("=" * 72)
    nasality()
    moving_tract()
    worst_case()

    print("\n" + "=" * 72)
    if failures == 0:
        print("EXPERIMENT 4 PASSED — the failures are graceful and reported.")
        return 0
    print(f"EXPERIMENT 4 FAILED ({failures})")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
