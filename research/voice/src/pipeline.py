"""Stages 1-7 assembled: audio in, VoiceInstrumentModel out.

This is where the separate stages stop being a pile of functions and become the
artifact the synthesiser plays. Two rules it enforces that the individual stages
cannot:

REFUSE RATHER THAN GUESS. If there is no stable region, or too few harmonics, the
model comes back with NOT_ENOUGH_AUDIO and the caller is expected to decline. A
plausible-looking instrument built from insufficient evidence is worse than no
instrument, because the user cannot tell the difference until they play it.

MODULATION IS MEASURED, NOT BAKED IN. Vibrato and tremolo are extracted as
separate LFOs and REMOVED from the static model. If the vibrato were left in the
harmonic amplitudes, every note the instrument played would carry the exact
vibrato of the recording, welded to it, at the wrong rate for any other tempo.
"""
from __future__ import annotations

import numpy as np

from analysis import (HOP_S, extract_harmonics_multi, find_stable_region,
                      frame_features, track_f0)
from envelope import DEFAULT_FREQS, estimate_envelope, find_formant_peaks
from model import (AmplitudeEnvelope, Confidence, DetectedLFO, Formant,
                   FormantModel, LFOTarget, NoiseModel, SourceMetadata,
                   VoiceInstrumentModel)


def _noise_model(audio, sr, pitch, start_s, end_s, f0_hz, envelope) -> NoiseModel:
    """Harmonic-to-noise ratio, and the noise's own spectral shape.

    Measured as the energy sitting BETWEEN harmonics versus the energy at them.
    Breath is not a fixed hiss added to a voice; it is turbulence shaped by the
    same tract, which is why it is stored as a spectrum to re-generate rather
    than as a loop of the recording.
    """
    noise = NoiseModel()
    a, b = int(start_s * sr), min(int(end_s * sr), audio.size)
    if b - a < 4096 or f0_hz <= 0:
        return noise

    n_fft = 4096
    segment = audio[a:a + n_fft] * np.hanning(n_fft)
    mag = np.abs(np.fft.rfft(segment))
    hz = np.fft.rfftfreq(n_fft, 1.0 / sr)
    mag_db = 20.0 * np.log10(np.maximum(mag, 1e-12))

    # Bins within a quarter-harmonic of a multiple of f0 are "at a harmonic";
    # the rest are the floor between them.
    nearest = np.round(hz / f0_hz)
    distance = np.abs(hz - nearest * f0_hz)
    at_harmonic = (distance < f0_hz * 0.25) & (nearest >= 1)
    between = (~at_harmonic) & (hz > f0_hz) & (hz < sr * 0.45)
    if not np.any(between) or not np.any(at_harmonic):
        return noise

    noise.harmonic_to_noise_db = float(np.median(mag_db[at_harmonic])
                                       - np.median(mag_db[between]))
    noise.overall_level_db = float(np.median(mag_db[between]))
    grid = DEFAULT_FREQS
    noise.frequency_hz = grid.tolist()
    noise.magnitude_db = np.interp(grid, hz[between], mag_db[between]).tolist()
    noise.confidence = (Confidence.DETECTED if noise.harmonic_to_noise_db > 3.0
                        else Confidence.LOW)
    return noise


def _amplitude_envelope(features, sr) -> AmplitudeEnvelope:
    """The measured loudness curve, with ADSR derived from it for the UI."""
    env = AmplitudeEnvelope()
    rms = np.asarray(features["rms"])
    times = np.asarray(features["times_s"])
    if rms.size < 8:
        return env

    peak = float(np.max(rms))
    if peak <= 0:
        return env
    level = rms / peak
    env.times_s, env.level = times.tolist(), level.tolist()

    # Attack: first crossing of 90%. Release: last fall back through 50%.
    above = np.flatnonzero(level > 0.9)
    env.attack_s = float(times[above[0]]) if above.size else 0.0
    sustained = np.flatnonzero(level > 0.5)
    if sustained.size:
        env.release_s = float(times[-1] - times[sustained[-1]])
        env.sustain_level = float(np.median(level[sustained]))
    env.confidence = Confidence.DETECTED
    return env


def _detect_lfo(times, values, unit, destination, min_hz=3.0,
                max_hz=9.0) -> DetectedLFO | None:
    """Periodic modulation in a control curve, stored as its ACTUAL shape.

    The folded shape table is the point of this function. Replacing a measured
    vibrato with a sine loses the asymmetry — singers spend longer at the
    extremes than a sine does — and that asymmetry is a large part of why one
    singer's vibrato sounds like a person and another's sounds like an LFO.
    """
    if times.size < 32:
        return None
    values = values - np.mean(values)
    dt = float(np.median(np.diff(times)))
    if dt <= 0:
        return None

    spectrum = np.abs(np.fft.rfft(values * np.hanning(values.size)))
    freqs = np.fft.rfftfreq(values.size, dt)
    band = (freqs >= min_hz) & (freqs <= max_hz)
    if not np.any(band) or np.max(spectrum[band]) <= 0:
        return None

    rate = float(freqs[band][np.argmax(spectrum[band])])
    peak = float(np.max(spectrum[band]))
    # Compare against the rest of the modulation band, not against zero: a curve
    # with no periodicity still has a largest bin, and calling that a vibrato is
    # how an instrument acquires a 4 Hz wobble its source never had.
    elsewhere = float(np.median(spectrum[(freqs > 0.5) & (freqs < 20.0)]))
    if peak < 4.0 * max(elsewhere, 1e-9):
        return None

    cycles = float((times[-1] - times[0]) * rate)
    lfo = DetectedLFO(rate_hz=rate, cycles_observed=cycles)
    # Fold every observed cycle onto one and average: this is what separates a
    # real repeated shape from a single wobble that happened once.
    phase = np.mod((times - times[0]) * rate, 1.0)
    table = np.zeros(128)
    counts = np.zeros(128)
    idx = np.minimum((phase * 128).astype(int), 127)
    np.add.at(table, idx, values)
    np.add.at(counts, idx, 1.0)
    seen = counts > 0
    table[seen] /= counts[seen]
    if np.any(~seen):
        table[~seen] = np.interp(np.flatnonzero(~seen), np.flatnonzero(seen),
                                 table[seen])
    depth = float(np.max(np.abs(table)))
    lfo.shape_table = (table / depth).tolist() if depth > 0 else table.tolist()
    lfo.targets = [LFOTarget(destination=destination, depth=depth, unit=unit)]
    # Under ~2.5 cycles a "modulation" is indistinguishable from the note's own
    # envelope, so it is reported as an estimate and never as a detection.
    lfo.confidence = (Confidence.DETECTED if cycles >= 2.5 else Confidence.ESTIMATED)
    return lfo


def analyse_voice(audio: np.ndarray, sr: int, filename: str = "",
                  envelope_method: str = "lpc") -> VoiceInstrumentModel:
    """The whole analysis chain."""
    m = VoiceInstrumentModel()
    m.source = SourceMetadata(filename=filename, sample_rate=sr,
                              duration_s=audio.size / sr, channels=1,
                              peak_dbfs=20.0 * np.log10(
                                  max(float(np.max(np.abs(audio))), 1e-9)))

    features = frame_features(audio, sr)
    m.pitch = track_f0(audio, sr)
    if m.pitch.overall_confidence == Confidence.NOT_ENOUGH_AUDIO:
        m.warn("no reliable pitch; this does not sound like a sustained note")
        return m

    m.stable_region = find_stable_region(features, m.pitch, sr)
    if m.stable_region.confidence == Confidence.NOT_ENOUGH_AUDIO:
        m.warn(f"no stable sustain: {m.stable_region.reason}")
        return m

    a_s, b_s = m.stable_region.start_s, m.stable_region.end_s
    m.harmonics = extract_harmonics_multi(audio, sr, m.pitch, a_s, b_s)
    if m.harmonics.n_harmonics < 4:
        m.warn("fewer than 4 harmonics; not enough to describe a timbre")
        m.analysis_confidence = Confidence.NOT_ENOUGH_AUDIO
        return m

    m.envelope = estimate_envelope(audio, sr, m.pitch, a_s, b_s,
                                   envelope_method, harmonics=m.harmonics)
    m.formants = FormantModel(
        formants=[Formant(center_hz=hz, confidence=Confidence.ESTIMATED)
                  for hz in find_formant_peaks(m.envelope)],
        confidence=Confidence.ESTIMATED)
    m.noise = _noise_model(audio, sr, m.pitch, a_s, b_s,
                           m.harmonics.f0_hz, m.envelope)
    m.amplitude = _amplitude_envelope(features, sr)

    # --- modulation, over the stable region only ---
    pt = np.asarray(m.pitch.times_s)
    ph = np.asarray(m.pitch.frequency_hz)
    inside = (pt >= a_s) & (pt <= b_s) & (ph > 0)
    if np.count_nonzero(inside) > 32:
        cents = 1200.0 * np.log2(ph[inside] / np.median(ph[inside]))
        lfo = _detect_lfo(pt[inside], cents, "cents", "pitch")
        if lfo:
            m.lfos.append(lfo)

    ft = np.asarray(features["times_s"])
    fd = np.asarray(features["rms_db"])
    inside = (ft >= a_s) & (ft <= b_s)
    if np.count_nonzero(inside) > 32:
        lfo = _detect_lfo(ft[inside], fd[inside], "db", "amplitude")
        if lfo:
            m.lfos.append(lfo)

    weakest = min(m.envelope.confidence, m.pitch.overall_confidence,
                  key=lambda c: list(Confidence).index(c))
    m.analysis_confidence = weakest
    if m.harmonics.reliable_up_to_hz < 3000.0:
        m.warn(f"harmonics only reliable to {m.harmonics.reliable_up_to_hz:.0f} Hz; "
               "brightness above that is extrapolated")
    return m
