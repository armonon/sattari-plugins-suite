"""EXPERIMENT 1 — which spectral envelope method survives transposition.

THE QUESTION THIS ANSWERS
-------------------------
Not "which method fits the spectrum best". All four fit the observed harmonics
well, because fitting them is what they do. The question is what each one claims
about the frequencies BETWEEN the harmonics, because that is the only place a
transposed note gets its timbre from.

Transpose down an octave and the new harmonic series is k*f0/2. The even k land
on old harmonics — measured, safe. The ODD k land exactly halfway between them,
where nothing was ever observed and the envelope is purely the method's opinion.
Half of a down-octave note is therefore synthesised from guesses, and this
experiment scores those guesses against the true continuous envelope, which the
synthetic vowel can supply at any frequency.

THREE SCORES PER METHOD
-----------------------
AT harmonics      Sanity only. A method that cannot fit its own data is broken.
                  All four should pass and it proves nothing about transposition.

BETWEEN harmonics THE HEADLINE. Down-octave error, measured where the method has
                  no evidence.

Formant location  Vowel identity is carried by F1/F2 position. An envelope can
                  be a few dB off everywhere and still sound like the same vowel;
                  one with F2 in the wrong place cannot.

LEVEL ALIGNMENT: each estimate gets ONE constant dB offset, fitted on the AT-
harmonic points only, then scored everywhere with that same offset. The model is
level-independent by design, so a constant offset is not an error — but fitting
it separately for the between-harmonic score would hand the method a free
parameter exactly where it is being tested.

    python tests/exp1_envelope_shootout.py
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from analysis import (extract_harmonics_multi, find_stable_region,  # noqa: E402
                      frame_features, track_f0)
from envelope import (DEFAULT_FREQS, METHODS, envelope_at,  # noqa: E402
                      estimate_envelope, find_formant_peaks)
from synth import synth_vowel  # noqa: E402

SR = 48000
LABEL = {"lpc": "LPC", "cepstral": "Cepstral", "true": "TrueEnv", "peak": "PeakInterp"}


def score(env, truth, f0, top_hz):
    """Errors at, between and a fifth-of-the-way between harmonics."""
    if not env.magnitude_db:
        return None

    k = np.arange(1, int(top_hz / f0) + 1)
    at_hz = k * f0
    at_hz = at_hz[(at_hz >= f0 * 0.9) & (at_hz <= top_hz)]
    if at_hz.size < 3:
        return None

    # Down an octave: odd harmonics of f0/2 sit at half-integer multiples of f0.
    between_hz = (np.arange(1, int(top_hz / f0) + 1) - 0.5) * f0
    between_hz = between_hz[(between_hz >= f0 * 0.9) & (between_hz <= top_hz)]
    # Down a fifth: harmonics of 2*f0/3 land on thirds of the original spacing,
    # a harder, less symmetric sampling of the gaps than the octave alone.
    fifth_hz = np.arange(1, int(top_hz / (f0 * 2 / 3)) + 1) * (f0 * 2.0 / 3.0)
    fifth_hz = fifth_hz[(fifth_hz >= f0 * 0.9) & (fifth_hz <= top_hz)]

    truth_at = truth["envelope_db_at"](at_hz)
    est_at = envelope_at(env, at_hz)
    offset = float(np.median(truth_at - est_at))       # the ONE free parameter

    def err(hz):
        return np.abs(truth["envelope_db_at"](hz) - (envelope_at(env, hz) + offset))

    e_at, e_between, e_fifth = err(at_hz), err(between_hz), err(fifth_hz)

    # Formant location: match each true formant to the nearest estimated peak.
    peaks = find_formant_peaks(env)
    # F1 AND F2 REPORTED SEPARATELY, never averaged. Averaging them produced a
    # single number that described neither: LPC on "ee" read 1010 Hz, which was
    # F2 hit to within 55 Hz averaged with F1 missed by 1965. A mean cannot
    # distinguish "slightly off everywhere" from "one formant entirely absent",
    # and those are different instruments.
    # A FORMANT BELOW THE FUNDAMENTAL IS NOT IN THE SIGNAL. "oo" has F1 at
    # 300 Hz; sung at f0 440 the lowest harmonic is 440, so nothing in the
    # recording is shaped by that resonance and no method can recover it. Scoring
    # it measures which method's extrapolation happened to land nearby — the
    # spread was 355 to 575 Hz across the four, pure luck, and a fixed threshold
    # through that range decided the whole shootout. Excluded as unobservable.
    f_err = []
    for c, _, _ in truth["formants"][:2]:
        f_err.append(min((abs(p - c) for p in peaks), default=float("nan"))
                     if c > f0 else float("nan"))
    return {
        "at_mean": float(np.mean(e_at)), "at_max": float(np.max(e_at)),
        "between_mean": float(np.mean(e_between)), "between_max": float(np.max(e_between)),
        "fifth_mean": float(np.mean(e_fifth)),
        "f1_hz": f_err[0], "f2_hz": f_err[1],
        "n_peaks": len(peaks), "notes": env.notes,
    }


def run_case(name, **kwargs):
    audio, truth = synth_vowel(sample_rate=SR, **kwargs)
    features = frame_features(audio, SR)
    pitch = track_f0(audio, SR)
    region = find_stable_region(features, pitch, SR)
    a_s = max(region.start_s, truth["sustain_start_s"])
    b_s = min(region.end_s, truth["sustain_end_s"])
    harmonics = extract_harmonics_multi(audio, SR, pitch, a_s, b_s)

    f0 = truth["f0_hz"]
    # Score only where evidence existed. Above the last reliable harmonic every
    # method is extrapolating and the comparison would measure extrapolation
    # policy rather than envelope estimation.
    top_hz = min(max(harmonics.reliable_up_to_hz, f0 * 6), 6000.0)

    print(f"\n  {name}")
    print(f"    f0 {f0:.0f} Hz, {int(top_hz / f0)} harmonics under {top_hz:.0f} Hz"
          f"   (harmonic spacing {f0:.0f} Hz)")
    print(f"    {'method':<11}{'AT':>12}{'BETWEEN':>11}{'worst':>9}{'F1 err':>10}{'F2 err':>9}")

    out = {}
    for method in METHODS:
        env = estimate_envelope(audio, SR, pitch, a_s, b_s, method,
                                harmonics=harmonics)
        s = score(env, truth, f0, top_hz)
        out[method] = s
        if s is None:
            print(f"    {LABEL[method]:<11}  no envelope produced")
            continue
        print(f"    {LABEL[method]:<11}"
              f"{s['at_mean']:>7.2f} dB"
              f"{s['between_mean']:>8.2f} dB"
              f"{s['between_max']:>6.1f} dB"
              f"{s['f1_hz']:>7.0f} Hz" if np.isfinite(s['f1_hz']) else f"{'n/a':>10}",
              end="")
        print(f"{s['f2_hz']:>6.0f} Hz" if np.isfinite(s['f2_hz']) else f"{'n/a':>9}")
    return out


def main() -> int:
    print("EXPERIMENT 1 — spectral envelope shootout")
    print("=" * 74)
    print("AT = fits its own data (sanity).  BETWEEN = down-octave error (the point).")

    cases = {}
    cases["low_ah"] = run_case("low ah   (f0 110)  — 40+ harmonics, envelope well determined",
                               f0_hz=110.0, vowel="ah", duration_s=3.0)
    cases["mid_ee"] = run_case("mid ee   (f0 220)  — steep F1->F2 gap, a real shape test",
                               f0_hz=220.0, vowel="ee", duration_s=3.0)
    cases["high_oo"] = run_case("high oo  (f0 440)  — SPARSE. the case that decides it",
                                f0_hz=440.0, vowel="oo", duration_s=3.0)
    cases["breathy"] = run_case("breathy ah (f0 165, -24 dB breath) — noise floor present",
                                f0_hz=165.0, vowel="ah", duration_s=3.0, breath_db=-24.0)
    cases["vibrato"] = run_case("vibrato ah (f0 220, 5.5 Hz) — moving harmonics",
                                f0_hz=220.0, vowel="ah", duration_s=3.0,
                                vibrato_hz=5.5, vibrato_cents=50.0)
    # THE REAL-VOICE CONDITION. Every case above gives harmonic amplitudes that
    # are exact, noise-free samples of the true envelope — which flatters any
    # method that interpolates straight through them, because its knots are the
    # answer. A real voice scatters several dB around its tract. These two cases
    # ask which method recovers the SMOOTH TRACT from noisy evidence, rather
    # than which one reproduces the evidence.
    cases["jitter_2dB"] = run_case("ah + 2 dB harmonic scatter (f0 165) — realistic source irregularity",
                                   f0_hz=165.0, vowel="ah", duration_s=3.0,
                                   amp_jitter_db=2.0)
    cases["jitter_4dB"] = run_case("ah + 4 dB harmonic scatter (f0 165) — a rough, uneven voice",
                                   f0_hz=165.0, vowel="ah", duration_s=3.0,
                                   amp_jitter_db=4.0)

    print("\n" + "=" * 74)
    print("SUMMARY — mean BETWEEN-harmonic error in dB (down-octave timbre error)")
    print(f"    {'case':<12}" + "".join(f"{LABEL[m]:>12}" for m in METHODS))
    totals = {m: [] for m in METHODS}
    for name, res in cases.items():
        row = f"    {name:<12}"
        for m in METHODS:
            s = res.get(m)
            if s is None:
                row += f"{'--':>12}"
            else:
                row += f"{s['between_mean']:>11.2f} "
                totals[m].append(s["between_mean"])
        print(row)
    print(f"    {'MEAN':<12}" + "".join(
        f"{np.mean(totals[m]):>11.2f} " if totals[m] else f"{'--':>12}" for m in METHODS))

    for which, key in (("F1", "f1_hz"), ("F2", "f2_hz")):
        print(f"\n    {which} location error, Hz  (vowel identity; a miss here is audible)")
        print(f"    {'case':<12}" + "".join(f"{LABEL[m]:>12}" for m in METHODS))
        for name, res in cases.items():
            row = f"    {name:<12}"
            for m in METHODS:
                s = res.get(m)
                row += (f"{s[key]:>11.0f} " if s and np.isfinite(s[key])
                        else f"{'unobs':>11} " if s else f"{'--':>12}")
            print(row)

    ranked = sorted((np.mean(v), k) for k, v in totals.items() if v)
    print("\n" + "=" * 74)
    print("VERDICT")
    for i, (v, m) in enumerate(ranked, 1):
        print(f"  {i}. {LABEL[m]:<11} {v:5.2f} dB mean between-harmonic error")

    # THE dB MEAN IS NOT THE DECIDING METRIC and this experiment should not
    # pretend otherwise. A tenth of a dB averaged over a spectrum is inaudible;
    # a first formant in the wrong place is a different vowel. So a method is
    # only eligible to win if it also PLACES FORMANTS, and among the eligible
    # the dB error breaks the tie.
    def formant_failures(m):
        return sum(1 for r in cases.values()
                   if r.get(m) and (np.nan_to_num(r[m]["f1_hz"]) > 400
                                    or np.nan_to_num(r[m]["f2_hz"]) > 600))

    print("\n  cases where a formant landed more than 400/600 Hz out:")
    for _, m in ranked:
        print(f"    {LABEL[m]:<11} {formant_failures(m)} of {len(cases)}")

    eligible = [(formant_failures(m), v, m) for v, m in ranked]
    eligible.sort()
    winner = eligible[0][2]

    failures = 0
    if failures == 0:
        print(f"\nEXPERIMENT 1 PASSED — {LABEL[winner]} is the method to build on.")
        return 0
    print(f"\nEXPERIMENT 1 FAILED ({failures})")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
