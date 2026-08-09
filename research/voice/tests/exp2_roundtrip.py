"""EXPERIMENT 2 — analysis, resynthesis, and the transposition claim.

THE PRODUCT QUESTION, STATED AS A MEASUREMENT
---------------------------------------------
"Can a sustained vowel be reconstructed from a harmonic + spectral-envelope
model well enough that it remains recognizably the same vocal timbre?" — and
critically, does it stay the same voice AT A DIFFERENT PITCH?

Recognisability is not directly measurable without listeners, so this measures
the thing that causes it: FORMANT POSITION. Vowel identity and speaker identity
both live in where the resonances sit. If F1 and F2 come back in the same place
after a round trip, and STAY there after transposition, the timbre is preserved
in the only sense the model can control.

THE CONTROL IS THE POINT
------------------------
Every test here is run twice: once through the model, once through naive
resampling. Resampling moves formants BY THE TRANSPOSITION RATIO — an octave
down halves them. That is the chipmunk effect, and it is what the instrument
must not do. Measuring both makes "the envelope stays put" a demonstrated
number rather than a claim about the architecture.

An octave DOWN is the hard direction and is tested as such: it doubles the
harmonic count, and every odd new harmonic lands halfway between two that were
measured, in territory the envelope had to invent.

    python tests/exp2_roundtrip.py
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from analysis import frame_features, track_f0  # noqa: E402
from envelope import envelope_at  # noqa: E402
from pipeline import analyse_voice  # noqa: E402
from synthesis import render, render_by_resampling  # noqa: E402
from synth import synth_vowel  # noqa: E402

SR = 48000
failures = 0


def check(ok: bool, message: str) -> None:
    global failures
    if not ok:
        print(f"    FAIL {message}")
        failures += 1


def formants_of(audio, label=""):
    """Re-analyse rendered audio and report where its resonances ended up."""
    m = analyse_voice(audio, SR, label)
    return m, [f.center_hz for f in m.formants.formants]


def match(peaks, target):
    return min((abs(p - target) for p in peaks), default=float("nan"))


def main() -> int:
    print("EXPERIMENT 2 — round trip and transposition")
    print("=" * 72)

    # ------------------------------------------------------------------
    #  Source: a known vowel. "ah" has F1 730 / F2 1090, far enough apart
    #  to track individually at f0 165 and low enough that an octave down
    #  puts many new harmonics below them.
    # ------------------------------------------------------------------
    audio, truth = synth_vowel(f0_hz=165.0, vowel="ah", duration_s=3.0,
                               sample_rate=SR, breath_db=-30.0)
    true_f1, true_f2 = truth["formants"][0][0], truth["formants"][1][0]
    print(f"\n  SOURCE  f0 165 Hz  vowel 'ah'  true F1 {true_f1:.0f} / F2 {true_f2:.0f} Hz")

    model = analyse_voice(audio, SR, "source")
    print(f"    analysis: {model.analysis_confidence.value}, "
          f"{model.harmonics.n_harmonics} harmonics, "
          f"HNR {model.noise.harmonic_to_noise_db:.1f} dB")
    print(f"    envelope: {model.envelope.method} — {model.envelope.notes}")
    for w in model.warnings:
        print(f"    warning: {w}")
    check(model.analysis_confidence.value in ("detected", "estimated"),
          "the source did not analyse")

    src_peaks = [f.center_hz for f in model.formants.formants]
    print(f"    formants found: {[round(p) for p in src_peaks]}")
    check(match(src_peaks, true_f1) < 150, f"F1 not recovered from the source")

    # ------------------------------------------------------------------
    #  1. UNISON round trip. Sanity: does it come back at all?
    # ------------------------------------------------------------------
    print("\n  1. UNISON round trip (render at the original 165 Hz)")
    unison = render(model, 165.0, duration_s=2.0, sr=SR)
    m_uni, uni_peaks = formants_of(unison, "unison")
    pitch_err = 1200.0 * np.log2(max(m_uni.pitch.median_f0_hz, 1e-9) / 165.0)
    print(f"    pitch  {m_uni.pitch.median_f0_hz:6.1f} Hz ({pitch_err:+.1f} cents)")
    print(f"    F1 {match(uni_peaks, true_f1):5.0f} Hz off    "
          f"F2 {match(uni_peaks, true_f2):5.0f} Hz off    "
          f"peaks {[round(p) for p in uni_peaks]}")
    check(abs(pitch_err) < 20, "unison render is not at the right pitch")
    check(match(uni_peaks, true_f1) < 150, "unison round trip lost F1")
    check(match(uni_peaks, true_f2) < 250, "unison round trip lost F2")

    # ------------------------------------------------------------------
    #  2. OCTAVE DOWN — model vs resampling, side by side.
    # ------------------------------------------------------------------
    print("\n  2. OCTAVE DOWN to 82.5 Hz — the claim, and the control")
    modelled = render(model, 82.5, duration_s=2.0, sr=SR)
    m_dn, dn_peaks = formants_of(modelled, "down-model")
    resampled = render_by_resampling(audio, -12.0)
    m_rs, rs_peaks = formants_of(resampled, "down-resample")

    print(f"    {'':<22}{'pitch':>9}{'F1':>9}{'F2':>9}   (true F1 730 / F2 1090)")
    print(f"    {'model (envelope)':<22}{m_dn.pitch.median_f0_hz:>8.1f} "
          f"{match(dn_peaks, true_f1):>8.0f} {match(dn_peaks, true_f2):>8.0f}   "
          f"peaks {[round(p) for p in dn_peaks]}")
    print(f"    {'resampled (control)':<22}{m_rs.pitch.median_f0_hz:>8.1f} "
          f"{match(rs_peaks, true_f1):>8.0f} {match(rs_peaks, true_f2):>8.0f}   "
          f"peaks {[round(p) for p in rs_peaks]}")

    # THE DECIDING METRIC IS A RATIO, MEASURED AGAINST THE EXTRACTED ENVELOPE.
    #
    # Not against the synthetic truth. "ah" has F1 730 and F2 1090 only 360 Hz
    # apart, and at f0 165 the harmonics are 165 Hz apart — too coarse to
    # separate them, so the source analysis legitimately reports ONE peak at
    # 675 Hz where two resonances merged. Scoring the transposed render against
    # a formant that was never in the model punishes it for the wrong thing.
    #
    # The claim under test is "the envelope does not move with the pitch", so
    # the reference is the envelope that was actually extracted, and the number
    # is how far each of its peaks travelled: 1.0 means it stayed, 0.5 means it
    # followed the pitch down an octave.
    # MEASURED BY ALIGNING THE ENVELOPE CURVES, NOT BY MATCHING PEAK LISTS.
    #
    # Peak matching needs a correspondence between two sets of peaks, and that
    # correspondence breaks exactly when the test is working: resampling halved
    # this vowel's F1 to ~337 Hz, which fell below the detector, so source-peak
    # 675 matched to the surviving 1220 and produced a ratio of 1.81 that
    # dragged the median to 0.691 — reporting "barely moved" about an envelope
    # that had moved by exactly half.
    #
    # Cross-correlating the two envelopes on a LOG frequency axis needs no
    # correspondence at all. A uniform scaling of every frequency is a pure
    # translation in log-f, so the lag that best aligns the curves IS the ratio,
    # and missing or extra peaks only reduce the correlation rather than
    # inventing a wrong answer.
    def envelope_distance(env_a, env_b, lo=300.0, hi=4500.0, points=512):
        """Mean |dB| between two envelopes — "is this the same throat?"

        THIS REPLACED A CROSS-CORRELATION, and the reason is worth keeping. The
        obvious metric is the frequency RATIO between the two sets of formants,
        recovered by aligning the curves in log-f. It failed twice, both times
        for real reasons rather than coding slips:

          - Raw envelopes are dominated by the -6 dB/octave source tilt, and a
            monotone curve self-correlates best at zero lag regardless of where
            its bumps are. The control, whose formants had provably halved,
            scored a perfect 1.000.
          - Detrended, it still could not choose: with only two surviving peaks
            in the control, aligning them one formant to the LEFT scores about
            as well as the true octave shift. 1.73 and 0.50 are both defensible
            readings of two bumps.

        Ratio recovery is a correspondence problem, and correspondence is
        exactly what breaks when a formant drops below the detector. The actual
        question does not need it: if this is the same throat, the two envelopes
        LIE ON TOP OF EACH OTHER. Median-subtracted so overall level, which the
        model is deliberately independent of, does not count as a difference.
        """
        grid = np.geomspace(lo, hi, points)
        a = np.asarray(envelope_at(env_a, grid), dtype=float)
        b = np.asarray(envelope_at(env_b, grid), dtype=float)
        if a.size != points or b.size != points:
            return float("nan")
        return float(np.mean(np.abs((a - np.median(a)) - (b - np.median(b)))))

    model_dist = envelope_distance(model.envelope, m_dn.envelope)
    control_dist = envelope_distance(model.envelope, m_rs.envelope)
    print(f"\n    ENVELOPE DISTANCE from the source throat, 300-4500 Hz")
    print(f"      model      {model_dist:5.2f} dB   (0 = the same throat, lower pitch)")
    print(f"      resampled  {control_dist:5.2f} dB   (large = a different, smaller throat)")

    # The control must MISBEHAVE, or the experiment cannot tell the two apart
    # and a pass by the model would prove nothing.
    check(control_dist > 5.0,
          f"the resampling control barely moved the envelope ({control_dist:.2f} dB); "
          "it cannot act as a control")
    check(model_dist < 3.0,
          f"the model's envelope moved ({model_dist:.2f} dB); it is not holding position")
    check(model_dist < control_dist / 3.0,
          "the model was not meaningfully better than resampling")

    check(abs(1200.0 * np.log2(max(m_dn.pitch.median_f0_hz, 1e-9) / 82.5)) < 25,
          "octave-down render is not an octave down")

    # ------------------------------------------------------------------
    #  3. OCTAVE UP — the sparse direction.
    # ------------------------------------------------------------------
    print("\n  3. OCTAVE UP to 330 Hz — fewer harmonics, envelope under-sampled")
    up = render(model, 330.0, duration_s=2.0, sr=SR)
    m_up, up_peaks = formants_of(up, "up-model")
    up_dist = envelope_distance(model.envelope, m_up.envelope)
    print(f"    pitch {m_up.pitch.median_f0_hz:6.1f} Hz   envelope distance {up_dist:.2f} dB   "
          f"peaks {[round(p) for p in up_peaks]}")
    # Looser than the octave down on purpose: at 330 Hz the harmonics are 330 Hz
    # apart, so the envelope is sampled half as often and a formant can only be
    # located to within about half that spacing. This is an information limit,
    # not slack in the implementation.
    check(up_dist < 4.5, f"octave up moved the envelope ({up_dist:.2f} dB)")

    # ------------------------------------------------------------------
    #  4. Does the model survive being SAVED?
    # ------------------------------------------------------------------
    print("\n  4. Serialisation")
    import json
    blob = json.loads(model.to_json())
    print(f"    {len(model.to_json()) / 1024:.0f} kB, version {blob['version']}, "
          f"{len(blob['envelope']['magnitude_db'])} envelope points")
    check(blob["version"] == 1, "model did not serialise with its schema version")
    check(len(blob["envelope"]["magnitude_db"]) > 100, "envelope did not survive saving")

    # ------------------------------------------------------------------
    #  5. Modulation is MEASURED and SEPARATE, not welded in.
    # ------------------------------------------------------------------
    print("\n  5. Vibrato is extracted, not baked in")
    vib_audio, _ = synth_vowel(f0_hz=220.0, vowel="ah", duration_s=3.0,
                               sample_rate=SR, vibrato_hz=5.5, vibrato_cents=50.0)
    vib_model = analyse_voice(vib_audio, SR, "vibrato")
    pitch_lfos = [l for l in vib_model.lfos
                  if l.targets and l.targets[0].destination == "pitch"]
    if pitch_lfos:
        lfo = pitch_lfos[0]
        print(f"    detected {lfo.rate_hz:.2f} Hz (true 5.50), depth "
              f"{lfo.targets[0].depth:.0f} cents (true 50), "
              f"{lfo.cycles_observed:.1f} cycles, {lfo.confidence.value}")
        check(abs(lfo.rate_hz - 5.5) < 0.6, "vibrato rate is wrong")
        check(abs(lfo.targets[0].depth - 50.0) < 25.0, "vibrato depth is wrong")
    else:
        check(False, "vibrato was not detected at all")

    # A note rendered WITHOUT vibrato from a vibrato'd source must be steady.
    # If the modulation had been absorbed into the static model this fails.
    steady = render(vib_model, 220.0, duration_s=2.0, sr=SR, apply_vibrato=False)
    steady_pitch = track_f0(steady, SR)
    print(f"    render without vibrato: pitch spread {steady_pitch.cents_std:.1f} cents "
          f"(source was {vib_model.pitch.cents_std:.1f})")
    check(steady_pitch.cents_std < 12.0,
          "vibrato leaked into the static model; rendered notes still wobble")

    # And a steady source must not acquire a vibrato it never had.
    flat_audio, _ = synth_vowel(f0_hz=220.0, vowel="ah", duration_s=3.0, sample_rate=SR)
    flat_model = analyse_voice(flat_audio, SR, "flat")
    invented = [l for l in flat_model.lfos
                if l.targets and l.targets[0].destination == "pitch"
                and l.targets[0].depth > 8.0]
    print(f"    steady source: {len(invented)} pitch LFOs invented (want 0)")
    check(not invented, "a vibrato was invented for a source that had none")

    print("\n" + "=" * 72)
    if failures == 0:
        print("EXPERIMENT 2 PASSED — the envelope holds its position under transposition,")
        print("and resampling demonstrably does not.")
        return 0
    print(f"EXPERIMENT 2 FAILED ({failures})")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
