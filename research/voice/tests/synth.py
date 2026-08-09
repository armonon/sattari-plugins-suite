"""Synthetic vowels with KNOWN ground truth.

WHY THIS COMES BEFORE ANY REAL VOICE
------------------------------------
With a real recording the only available verdict is "does that spectrogram look
about right?", and a subtly broken analyzer passes that test every time. Here the
true F0, the true per-harmonic amplitudes and the true spectral envelope are
inputs, so analysis error is a NUMBER in cents and dB.

When Milestone 5 eventually sounds wrong on a real voice — and it will — the
question is which stage broke. Without a ground-truth path that question is
answered by guessing.

THE MODEL BEING SYNTHESISED
---------------------------
    excitation (harmonic series with spectral tilt)  x  vocal tract (formants)

which is deliberately the same decomposition the analyzer assumes. That makes
this a test of the ANALYSIS, not of the assumption. A real voice will violate
the assumption in ways this cannot show — nasality, anti-formants, non-stationary
tract — and that limitation is stated here rather than discovered later.
"""
from __future__ import annotations

import numpy as np

# --- Reference vowels -------------------------------------------------------
#  Formants as (centre Hz, bandwidth Hz, gain dB). Values are conventional adult
#  measurements; they need not be perfect, because they are the ground truth BY
#  DEFINITION here — what matters is that the analyzer recovers what was used.
VOWELS: dict[str, list[tuple[float, float, float]]] = {
    "ah": [(730, 90, 0.0), (1090, 110, -6.0), (2440, 140, -12.0), (3400, 180, -20.0)],
    "ee": [(270, 60, 0.0), (2290, 100, -8.0), (3010, 140, -14.0), (3700, 200, -22.0)],
    "oo": [(300, 60, 0.0), (870, 90, -10.0), (2240, 140, -18.0), (3300, 200, -26.0)],
    "oh": [(570, 80, 0.0), (840, 100, -5.0), (2410, 140, -16.0), (3300, 200, -24.0)],
}


def formant_envelope_db(freq_hz: np.ndarray,
                        formants: list[tuple[float, float, float]],
                        tilt_db_per_octave: float = -6.0,
                        ref_hz: float = 500.0) -> np.ndarray:
    """The TRUE envelope, in dB, at arbitrary frequencies.

    Each formant is a resonance whose magnitude response is evaluated directly,
    so the envelope is defined CONTINUOUSLY rather than only at harmonic
    positions. That is what lets the test ask the analyzer for values between
    the harmonics it observed — the exact thing down-transposition depends on.
    """
    freq = np.maximum(np.asarray(freq_hz, dtype=float), 1.0)
    total = np.zeros_like(freq)

    for centre, bandwidth, gain_db in formants:
        # Second-order resonance magnitude, evaluated on the real frequency axis.
        # |H| = 1 / sqrt((1 - (f/fc)^2)^2 + (f/(fc*Q))^2), Q = fc / bandwidth.
        q = centre / max(bandwidth, 1.0)
        ratio = freq / centre
        magnitude = 1.0 / np.sqrt((1.0 - ratio ** 2) ** 2 + (ratio / q) ** 2)
        total += (10.0 ** (gain_db / 20.0)) * magnitude

    envelope_db = 20.0 * np.log10(np.maximum(total, 1e-9))
    # Glottal source tilt: real voices lose energy with frequency, and an
    # analyzer that only ever sees flat sources will look better than it is.
    envelope_db += tilt_db_per_octave * np.log2(freq / ref_hz)
    return envelope_db


def synth_vowel(
    f0_hz: float = 220.0,
    vowel: str = "ah",
    duration_s: float = 3.0,
    sample_rate: int = 48000,
    tilt_db_per_octave: float = -6.0,
    breath_db: float = -120.0,
    amp_jitter_db: float = 0.0,
    vibrato_hz: float = 0.0,
    vibrato_cents: float = 0.0,
    tremolo_hz: float = 0.0,
    tremolo_db: float = 0.0,
    attack_s: float = 0.05,
    release_s: float = 0.15,
    seed: int = 0,
) -> tuple[np.ndarray, dict]:
    """Render a vowel and return (audio, ground_truth).

    Vibrato is applied by integrating instantaneous frequency into phase — NOT by
    modulating a fixed phase term. The shortcut produces frequency modulation
    with the wrong index and a vibrato depth that does not match the number you
    asked for, which would make the modulation tests measure the generator's bug
    instead of the analyzer's accuracy.
    """
    rng = np.random.default_rng(seed)
    n = int(duration_s * sample_rate)
    t = np.arange(n) / sample_rate

    # --- f0 trajectory ---
    f0 = np.full(n, float(f0_hz))
    if vibrato_hz > 0.0 and vibrato_cents > 0.0:
        f0 = f0_hz * 2.0 ** ((vibrato_cents / 1200.0) * np.sin(2 * np.pi * vibrato_hz * t))

    formants = VOWELS[vowel]
    # Only harmonics below Nyquist, with margin — an aliased harmonic would be
    # measured at the wrong frequency and blamed on the analyzer.
    max_harmonic = int((sample_rate * 0.45) / f0_hz)

    audio = np.zeros(n)
    true_freqs, true_amps_db = [], []

    for k in range(1, max_harmonic + 1):
        harmonic_hz = f0_hz * k
        amp_db = formant_envelope_db(np.array([harmonic_hz]), formants,
                                     tilt_db_per_octave)[0]
        # REAL VOICES DO NOT SIT ON A SMOOTH CURVE. Source irregularity, glottal
        # pulse shape and phase interaction scatter harmonic amplitudes several dB
        # around the tract envelope. Without this the synthetic harmonics ARE
        # noise-free samples of the true envelope, which hands any method that
        # interpolates through them a advantage no real recording would give.
        if amp_jitter_db > 0.0:
            amp_db += rng.normal(0.0, amp_jitter_db)
        amp = 10.0 ** (amp_db / 20.0)

        # Phase from the integral of instantaneous frequency.
        phase = 2.0 * np.pi * np.cumsum(f0 * k) / sample_rate
        phase += rng.uniform(0, 2 * np.pi)      # random start phase, as a real voice has
        audio += amp * np.sin(phase)

        true_freqs.append(harmonic_hz)
        true_amps_db.append(amp_db)

    # Normalise the harmonic part before adding noise, so breath_db is meaningful.
    peak = np.max(np.abs(audio)) or 1.0
    audio /= peak
    true_amps_db = np.asarray(true_amps_db) - 20.0 * np.log10(peak)

    # --- breath: noise shaped by the SAME tract, which is what makes it sound
    #     like the same voice rather than added hiss ---
    if breath_db > -119.0:
        noise = rng.normal(0.0, 1.0, n)
        spectrum = np.fft.rfft(noise)
        freqs = np.fft.rfftfreq(n, 1.0 / sample_rate)
        shape_db = formant_envelope_db(freqs, formants, tilt_db_per_octave)
        spectrum *= 10.0 ** (shape_db / 20.0)
        shaped = np.fft.irfft(spectrum, n)
        shaped /= (np.max(np.abs(shaped)) or 1.0)
        audio += (10.0 ** (breath_db / 20.0)) * shaped

    # --- tremolo ---
    if tremolo_hz > 0.0 and tremolo_db > 0.0:
        audio *= 10.0 ** ((tremolo_db * np.sin(2 * np.pi * tremolo_hz * t)) / 20.0)

    # --- attack / release, so region detection has something real to find ---
    env = np.ones(n)
    a = max(1, int(attack_s * sample_rate))
    r = max(1, int(release_s * sample_rate))
    env[:a] = np.linspace(0.0, 1.0, a) ** 2
    env[-r:] = np.linspace(1.0, 0.0, r) ** 2
    audio *= env

    audio /= (np.max(np.abs(audio)) or 1.0) * 1.02      # leave headroom, never clip

    truth = {
        "f0_hz": float(f0_hz),
        "vowel": vowel,
        "formants": formants,
        "tilt_db_per_octave": float(tilt_db_per_octave),
        "harmonic_freqs_hz": np.asarray(true_freqs),
        "harmonic_amps_db": np.asarray(true_amps_db),
        "n_harmonics": len(true_freqs),
        "vibrato_hz": float(vibrato_hz),
        "vibrato_cents": float(vibrato_cents),
        "tremolo_hz": float(tremolo_hz),
        "tremolo_db": float(tremolo_db),
        "breath_db": float(breath_db),
        "amp_jitter_db": float(amp_jitter_db),
        "sample_rate": int(sample_rate),
        "attack_s": float(attack_s),
        "release_s": float(release_s),
        "sustain_start_s": float(attack_s),
        "sustain_end_s": float(duration_s - release_s),
        # The continuous true envelope, callable at any frequency. This is what
        # makes it possible to score envelope estimates BETWEEN the harmonics —
        # the region that determines down-transposition quality.
        "envelope_db_at": lambda f: formant_envelope_db(
            np.asarray(f, dtype=float), formants, tilt_db_per_octave),
    }
    return audio.astype(np.float32), truth
