"""Play the model at any pitch.

THE ONE LINE THAT MAKES THIS AN INSTRUMENT AND NOT A SAMPLER
------------------------------------------------------------
    amplitude of harmonic k  =  envelope( k * f0_played )

NOT the stored amplitude of harmonic k. The stored amplitudes describe the note
that was recorded; the envelope describes the throat that produced it. Play a
different pitch and the harmonics move to new frequencies, where the envelope —
which does not move — gives them new amplitudes. That is what a real voice does
when it sings a different note, and it is why an octave down sounds like the
same person singing lower instead of like a slowed-down tape.

Resampling gets this exactly wrong: it moves the formants WITH the pitch, by the
same ratio, which is the chipmunk effect. Experiment 2 measures both against
each other rather than asserting the difference.
"""
from __future__ import annotations

import numpy as np

from envelope import envelope_at
from model import VoiceInstrumentModel


def render(model: VoiceInstrumentModel, f0_hz: float, duration_s: float = 2.0,
           sr: int = 48000, apply_vibrato: bool = False,
           apply_noise: bool = True, seed: int = 0) -> np.ndarray:
    """Render one note from the model.

    apply_vibrato is OFF by default. The measured vibrato is a property of the
    performance, not of the voice, and re-applying it to every note an
    instrument plays is the welded-in-modulation mistake the model exists to
    avoid. It is available because a player may want it.
    """
    rng = np.random.default_rng(seed)
    n = int(duration_s * sr)
    t = np.arange(n) / sr

    if not model.envelope.magnitude_db or f0_hz <= 0:
        return np.zeros(n, dtype=np.float32)

    # --- pitch trajectory ---
    f0_curve = np.full(n, float(f0_hz))
    if apply_vibrato:
        for lfo in model.lfos:
            if lfo.targets and lfo.targets[0].destination == "pitch" and lfo.shape_table:
                table = np.asarray(lfo.shape_table)
                phase = np.mod(t * lfo.rate_hz, 1.0)
                depth = lfo.targets[0].depth
                cents = depth * np.interp(phase * len(table),
                                          np.arange(len(table)), table,
                                          period=len(table))
                f0_curve = f0_hz * 2.0 ** (cents / 1200.0)

    nyquist = sr * 0.45
    max_k = int(nyquist / f0_hz)
    audio = np.zeros(n)

    # Never claim brightness that was never measured. Above the reliable range
    # the envelope is extrapolation, so its value is HELD rather than followed —
    # a rising extrapolated envelope would add sizzle the singer never produced,
    # and this becomes audible exactly when transposing DOWN, where many new
    # harmonics land in that region.
    ceiling_hz = model.harmonics.reliable_up_to_hz or nyquist
    ceiling_db = float(envelope_at(model.envelope, [ceiling_hz])[0])

    stored_phase = model.harmonics.phase_rad
    for k in range(1, max_k + 1):
        harmonic_hz = f0_hz * k
        if harmonic_hz > ceiling_hz:
            amp_db = ceiling_db - 6.0 * np.log2(harmonic_hz / ceiling_hz)
        else:
            amp_db = float(envelope_at(model.envelope, [harmonic_hz])[0])

        amp = 10.0 ** (amp_db / 20.0)
        if amp < 1e-5:
            continue

        phase = 2.0 * np.pi * np.cumsum(f0_curve * k) / sr
        phase += (stored_phase[k - 1] if k - 1 < len(stored_phase)
                  else rng.uniform(0, 2 * np.pi))
        audio += amp * np.sin(phase)

    peak = float(np.max(np.abs(audio))) or 1.0
    audio /= peak

    # --- breath, shaped by the same tract ---
    if apply_noise and model.noise.magnitude_db and model.noise.harmonic_to_noise_db > 0:
        noise = rng.normal(0.0, 1.0, n)
        spectrum = np.fft.rfft(noise)
        freqs = np.fft.rfftfreq(n, 1.0 / sr)
        shape_db = np.interp(freqs, np.asarray(model.noise.frequency_hz),
                             np.asarray(model.noise.magnitude_db))
        shape_db -= np.max(shape_db)
        spectrum *= 10.0 ** (shape_db / 20.0)
        shaped = np.fft.irfft(spectrum, n)
        shaped /= (np.max(np.abs(shaped)) or 1.0)
        audio += shaped * 10.0 ** (-model.noise.harmonic_to_noise_db / 20.0)

    # --- amplitude envelope, resampled onto this note's length ---
    if model.amplitude.level:
        src_t = np.asarray(model.amplitude.times_s)
        src_l = np.asarray(model.amplitude.level)
        if src_t.size > 1:
            audio *= np.interp(t, src_t / src_t[-1] * duration_s, src_l)
    else:
        attack = max(1, int(0.02 * sr))
        release = max(1, int(0.10 * sr))
        env = np.ones(n)
        env[:attack] = np.linspace(0.0, 1.0, attack)
        env[-release:] = np.linspace(1.0, 0.0, release)
        audio *= env

    audio /= (np.max(np.abs(audio)) or 1.0) * 1.02
    return audio.astype(np.float32)


def render_by_resampling(audio: np.ndarray, semitones: float) -> np.ndarray:
    """THE CONTROL — naive resampling, which moves the formants with the pitch.

    This is not a strawman. It is what a sampler does, what varispeed does, and
    what the instrument would do if the envelope were stored relative to f0
    instead of in absolute Hz. Experiment 2 uses it to prove the difference is
    real and measured rather than asserted.
    """
    import librosa
    ratio = 2.0 ** (semitones / 12.0)
    # Resample the CONTENT while keeping the playback rate: every frequency in
    # the result is scaled by `ratio`, formants included.
    return librosa.resample(np.asarray(audio, dtype=float),
                            orig_sr=10000, target_sr=int(10000 / ratio)
                            ).astype(np.float32)
