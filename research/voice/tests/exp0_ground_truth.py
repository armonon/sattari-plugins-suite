"""EXPERIMENT 0 — analysis accuracy against known ground truth.

Runs the method-independent stages (region, F0, harmonics) on synthetic vowels
whose true F0, true harmonic amplitudes and true envelope are inputs, and reports
error as NUMBERS rather than as a spectrogram to squint at.

This runs before any real voice, and before the spectral-envelope method is
chosen, because every envelope candidate is fed by these stages: an F0 that is
10 cents off or harmonic amplitudes that are 3 dB out would make all four
envelope methods look equally mediocre for reasons that have nothing to do with
the envelope.

    python tests/exp0_ground_truth.py
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from analysis import (extract_harmonics_multi, find_stable_region,  # noqa: E402
                      frame_features, track_f0)
from synth import synth_vowel  # noqa: E402

SR = 48000
failures = 0


def check(ok: bool, message: str) -> None:
    global failures
    if not ok:
        print(f"    FAIL {message}")
        failures += 1


def run_case(name: str, **kwargs) -> dict:
    audio, truth = synth_vowel(sample_rate=SR, **kwargs)
    print(f"\n  {name}")
    print(f"    truth: f0={truth['f0_hz']:.1f} Hz  vowel={truth['vowel']}  "
          f"{truth['n_harmonics']} harmonics  breath={truth['breath_db']:.0f} dB")

    features = frame_features(audio, SR)
    pitch = track_f0(audio, SR)
    region = find_stable_region(features, pitch, SR)

    # --- F0 accuracy -----------------------------------------------------
    f0_err_cents = float("nan")
    if pitch.median_f0_hz > 0:
        f0_err_cents = 1200.0 * np.log2(pitch.median_f0_hz / truth["f0_hz"])
    print(f"    f0:     {pitch.median_f0_hz:7.2f} Hz  "
          f"err {f0_err_cents:+6.2f} cents  ({pitch.overall_confidence.value})")

    # --- region ----------------------------------------------------------
    print(f"    region: {region.start_s:.2f}-{region.end_s:.2f}s "
          f"({region.confidence.value}: {region.reason})")

    # --- harmonic amplitudes --------------------------------------------
    # Multi-frame across the whole stable region, at each frame's own F0.
    a_s = max(region.start_s, truth["sustain_start_s"])
    b_s = min(region.end_s, truth["sustain_end_s"])
    harmonics = extract_harmonics_multi(audio, SR, pitch, a_s, b_s)

    # Compare on RELATIVE amplitudes: the model is level-independent by design,
    # so an absolute offset is not an error — the SHAPE is what must match.
    true_db = np.asarray(truth["harmonic_amps_db"])
    true_db = true_db - np.max(true_db)
    n = min(len(true_db), len(harmonics.amplitude_db))
    got_db = np.asarray(harmonics.amplitude_db[:n])
    ref_db = true_db[:n]

    # Only score harmonics that were above the noise floor; the rest are
    # honestly unmeasurable and scoring them measures the synthetic noise.
    reliable = np.asarray(harmonics.frequency_hz[:n]) <= max(
        harmonics.reliable_up_to_hz, truth["f0_hz"] * 8)
    err = np.abs(got_db[reliable] - ref_db[reliable])
    freq_err_cents = 1200.0 * np.log2(
        np.asarray(harmonics.frequency_hz[:n])[reliable]
        / np.asarray(truth["harmonic_freqs_hz"][:n])[reliable])

    print(f"    harmonics: {harmonics.n_harmonics} found, "
          f"{int(reliable.sum())} scored, reliable to "
          f"{harmonics.reliable_up_to_hz:.0f} Hz")
    if err.size == 0:
        # Report and move on rather than crashing on an empty reduction: a stage
        # that produced nothing is a RESULT, and the gates below should say so.
        print("    amplitude error: NO HARMONICS SCORED")
        return {"f0_err_cents": f0_err_cents, "amp_err_mean_db": float("inf"),
                "amp_err_median_db": float("inf"), "freq_err_max_cents": float("inf"),
                "region_ok": region.confidence.value in ("detected", "estimated"),
                "n_scored": 0}
    print(f"    amplitude error: mean {np.mean(err):.2f} dB  "
          f"median {np.median(err):.2f} dB  max {np.max(err):.2f} dB")
    print(f"    frequency error: max {np.max(np.abs(freq_err_cents)):.2f} cents")

    return {
        "f0_err_cents": f0_err_cents,
        "amp_err_mean_db": float(np.mean(err)),
        "amp_err_median_db": float(np.median(err)),
        "freq_err_max_cents": float(np.max(np.abs(freq_err_cents))),
        "region_ok": region.confidence.value in ("detected", "estimated"),
        "n_scored": int(reliable.sum()),
    }


def main() -> int:
    print("EXPERIMENT 0 — analysis vs known ground truth")
    print("=" * 66)

    results = {}

    # A low male-ish "ah": many harmonics, the easy case.
    results["low_ah"] = run_case("low ah  (f0 110, clean)",
                                 f0_hz=110.0, vowel="ah", duration_s=3.0)

    # Mid alto "ee": tight low formant, wide gap to F2 — a real test of whether
    # harmonic amplitudes are read correctly across a steep envelope.
    results["mid_ee"] = run_case("mid ee  (f0 220, clean)",
                                 f0_hz=220.0, vowel="ee", duration_s=3.0)

    # High "oo": FEW harmonics. This is the case that gets hard, and the one
    # where the envelope will be under-determined later.
    results["high_oo"] = run_case("high oo (f0 440, clean)",
                                  f0_hz=440.0, vowel="oo", duration_s=3.0)

    # Breath: harmonic measurement must survive a realistic noise floor.
    results["breathy"] = run_case("breathy ah (f0 165, -24 dB breath)",
                                  f0_hz=165.0, vowel="ah", duration_s=3.0,
                                  breath_db=-24.0)

    # Vibrato: F0 tracking and the region finder must not fall over on movement
    # that is entirely normal for a sung note.
    results["vibrato"] = run_case("vibrato ah (f0 220, 5.5 Hz / 50 cents)",
                                  f0_hz=220.0, vowel="ah", duration_s=3.0,
                                  vibrato_hz=5.5, vibrato_cents=50.0)

    print("\n" + "=" * 66)
    print("GATES")

    for name, r in results.items():
        # F0 within 5 cents: below the 5-15 cent detector-disagreement floor this
        # repo already measured on real voices, so anything tighter would be
        # measuring the synthetic signal rather than the tracker.
        check(abs(r["f0_err_cents"]) < 5.0,
              f"{name}: f0 error {r['f0_err_cents']:+.2f} cents exceeds 5")
        # 1.5 dB median on harmonic amplitudes: the envelope is fitted THROUGH
        # these, so their error is the floor for every envelope method.
        check(r["amp_err_median_db"] < 1.5,
              f"{name}: median amplitude error {r['amp_err_median_db']:.2f} dB exceeds 1.5")
        check(r["freq_err_max_cents"] < 10.0,
              f"{name}: harmonic frequency error {r['freq_err_max_cents']:.2f} cents exceeds 10")
        check(r["region_ok"], f"{name}: no usable stable region found")
        check(r["n_scored"] >= 4, f"{name}: only {r['n_scored']} harmonics scored")

    if failures == 0:
        print("  all gates passed")
        print("\nEXPERIMENT 0 PASSED — the stages feeding the envelope are trustworthy.")
        return 0
    print(f"\nEXPERIMENT 0 FAILED ({failures} gate failures)")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
