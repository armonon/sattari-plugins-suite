"""Stage 4 — spectral envelope, four candidate methods.

WHY FOUR AND NOT ONE
--------------------
The envelope IS the instrument. It is the one part of the model that has to stay
fixed in absolute Hz while the harmonics slide underneath it, and every claim
about "the same voice at a different pitch" reduces to whether the envelope was
right at frequencies where no harmonic was ever observed.

That last clause is why guessing at a method would have been irresponsible. All
four fit the observed harmonics well by construction — that is what fitting
means. They differ almost entirely BETWEEN the harmonics, which is invisible
until you transpose down and half the new harmonics land in the gaps.

THE FOUR
--------
LPC             An all-pole vocal-tract model. Physically motivated: poles ARE
                resonances. Fails on nasality (needs zeros) and is sensitive to
                order — too low smooths formants together, too high starts
                fitting individual harmonics, which is the failure that looks
                best on the fitting data and worst after transposition.

CEPSTRAL        Low-quefrency lifter. Cheap and unconditionally stable, no order
                to get wrong beyond the lifter cutoff. Systematically biased
                LOW: it averages through the harmonic peaks and valleys instead
                of riding the peaks, so formants come out shallow.

TRUE ENVELOPE   Iterative cepstral (Roebel/Rodet). Repeatedly replaces the
                spectrum with max(spectrum, current envelope) before liftering,
                which drags the estimate up onto the peaks and removes the
                cepstral bias. Costs iterations.

PEAK INTERP     Interpolate straight through the measured harmonic amplitudes.
                Exactly right where harmonics exist and pure guesswork between
                them — the guess being a smooth curve, which a formant skirt is
                not. Degrades as f0 rises and harmonics thin out.

ALL FOUR RUN PER FRAME AND ARE MEDIANED, for the same reason the harmonic
extractor does: a single long window smears a vibrato'd note. Uniform treatment
also means the comparison is between ENVELOPE METHODS and not between framing
strategies.
"""
from __future__ import annotations

import numpy as np
import scipy.signal
from scipy.interpolate import PchipInterpolator

from model import Confidence, HarmonicModel, PitchTrack, SpectralEnvelope

# Evaluation grid. 8 kHz because that is where vocal-tract resonances live and
# where LPC-after-decimation can speak; 5 Hz steps resolve a 60 Hz-bandwidth
# formant with twelve points across it.
DEFAULT_FREQS = np.arange(50.0, 8000.0, 5.0)

N_FFT = 2048        # same window as harmonic extraction, deliberately
HOP_S = 0.02


# =============================================================================
#  Per-frame primitives
# =============================================================================
def _frame_spectrum_db(frame: np.ndarray, sr: int, n_fft: int = N_FFT):
    """Windowed log-magnitude spectrum and its frequency axis."""
    x = np.zeros(n_fft)
    take = min(n_fft, frame.size)
    x[:take] = frame[:take]
    x *= np.hanning(n_fft)
    mag = np.abs(np.fft.rfft(x))
    return 20.0 * np.log10(np.maximum(mag, 1e-12)), np.fft.rfftfreq(n_fft, 1.0 / sr)


def lpc_order_for(f0_hz: float, work_sr: int = 16000) -> int:
    """LPC order as a function of PITCH, not of sample rate.

    The textbook rule is order = sr/1000 + 4, which is f0-independent and
    therefore wrong for singing. An order sweep against known ground truth:

        f0 110 "ah"   order 20 -> 0.56 dB   order 40 -> 0.17 dB
        f0 220 "ee"   order 20 -> F1 MISSED BY 1965 Hz   order 30 -> 25 Hz
        f0 440 "oo"   order 20 -> 1.74 dB   order 40 -> 35.08 dB

    That last row is the whole point. At order 40 with only 18 harmonics under
    Nyquist, LPC put its peaks at 440, 880, 2200, 3520 Hz — the HARMONIC
    frequencies. It had stopped modelling the vocal tract and started modelling
    the harmonic comb, one pole per partial. Its formant score IMPROVED while
    the envelope became unusable, which is exactly how this failure hides: the
    model fits the observed spectrum beautifully and describes a voice that does
    not exist between the harmonics.

    So the constraint is evidence, not bandwidth. Each resonance costs a pole
    pair, and a pole pair needs roughly three harmonics to be pinned down rather
    than wrapped around a single partial:

        order <= 2/3 x (harmonics below the working Nyquist)

    which yields 40 at 110 Hz, 32 at 165, 24 at 220 and 12 at 440 — near the
    measured optimum in every row of that sweep.
    """
    harmonics = max(4.0, (work_sr * 0.5) / max(f0_hz, 40.0))
    return int(np.clip(round(2.0 * harmonics / 3.0), 10, 40))


def _lpc_envelope(frame: np.ndarray, sr: int, freqs: np.ndarray,
                  order: int = 0, work_sr: int = 16000,
                  f0_hz: float = 0.0, pre_emphasis: float = 0.5) -> np.ndarray | None:
    """All-pole spectral envelope.

    PRE-EMPHASIS 0.5, NOT THE TEXTBOOK 0.97. Pre-emphasis exists to flatten the
    glottal source tilt so LPC spends its poles on resonances instead of on the
    slope. At 0.97 it over-corrects and tilts the low end DOWN, and a formant
    that sits low enough simply stops being worth a pole. Measured on "ee",
    whose F1 is at 270 Hz:

        pre-emphasis 0.97   F1 MISSED BY 1990 Hz    0.74 dB between harmonics
        pre-emphasis 0.50   F1 found within 40 Hz   0.97 dB
        pre-emphasis 0.00   F1 found within 40 Hz   1.08 dB

    0.23 dB is inaudible. A first formant in the wrong place is a DIFFERENT
    VOWEL. The trade is not close, even though the dB column prefers 0.97 —
    which is precisely why the scoring reports formant position separately
    instead of averaging everything into one number.

    The real fix is a pre-emphasis coefficient fitted to each voice's measured
    spectral tilt rather than any fixed constant; 0.5 is the best fixed value
    found here, and the tilt IS already measured elsewhere in the model.

    DECIMATED TO 16 kHz FIRST, which is standard practice and not an
    optimisation. At 48 kHz an order-p model spends its poles across 24 kHz, and
    everything above 8 kHz is breath and noise with no resonances in it — so
    two thirds of the model's capacity would be fitting the part we do not care
    about, starving the formant region that we do. Decimating puts Nyquist at
    8 kHz so every pole is spent where the vocal tract actually is.
    """
    if sr % work_sr == 0:
        decimated = scipy.signal.resample_poly(frame, 1, sr // work_sr)
    else:
        decimated = scipy.signal.resample_poly(frame, work_sr, sr)
    if decimated.size < 64:
        return None

    if order <= 0:
        order = lpc_order_for(f0_hz, work_sr)

    windowed = decimated * np.hanning(decimated.size)
    if pre_emphasis > 0.0:
        windowed = np.append(windowed[0], windowed[1:] - pre_emphasis * windowed[:-1])

    try:
        import librosa
        a = librosa.lpc(np.ascontiguousarray(windowed, dtype=np.float64), order=order)
    except Exception:
        return None
    if not np.all(np.isfinite(a)):
        return None

    inside = freqs < (work_sr * 0.5 - 50.0)
    w, h = scipy.signal.freqz([1.0], a, worN=freqs[inside], fs=work_sr)
    env = np.full(freqs.shape, np.nan)
    env[inside] = 20.0 * np.log10(np.maximum(np.abs(h), 1e-12))
    # Undo the pre-emphasis tilt so the envelope describes the signal, not the
    # pre-emphasised copy of it. Skipping this adds a clean +6 dB/octave error
    # that a constant-offset alignment cannot remove.
    if pre_emphasis > 0.0:
        with np.errstate(divide="ignore"):
            deemph = 20.0 * np.log10(np.maximum(np.abs(
                1.0 - pre_emphasis * np.exp(-2j * np.pi * freqs / work_sr)), 1e-9))
        env -= deemph
    if np.any(np.isnan(env)):
        good = ~np.isnan(env)
        if not np.any(good):
            return None
        env[~good] = env[good][-1]
    return env


def _lifter(spectrum_db: np.ndarray, n_cep: int) -> np.ndarray:
    """Keep the first n_cep cepstral coefficients; discard the rest."""
    cep = np.fft.irfft(spectrum_db)
    cep[n_cep:-n_cep] = 0.0
    return np.fft.rfft(cep).real


def _cepstral_envelope(frame: np.ndarray, sr: int, freqs: np.ndarray,
                       f0_hz: float) -> np.ndarray:
    """Single-pass low-quefrency lifter."""
    spec_db, bin_hz = _frame_spectrum_db(frame, sr)
    # The lifter cutoff must sit BELOW the pitch quefrency or it passes the
    # harmonic comb straight through. Half a pitch period is the usual choice
    # and keeps enough coefficients to describe four formants.
    n_cep = max(8, int(0.5 * sr / max(f0_hz, 40.0)))
    n_cep = min(n_cep, len(spec_db) // 2 - 1)
    return np.interp(freqs, bin_hz, _lifter(spec_db, n_cep))


def _true_envelope(frame: np.ndarray, sr: int, freqs: np.ndarray,
                   f0_hz: float, max_iter: int = 100,
                   tol_db: float = 0.5) -> np.ndarray:
    """Iterative cepstral (Roebel & Rodet).

    Each pass replaces the spectrum with max(spectrum, envelope) before
    liftering. The valleys between harmonics get filled in with the current
    estimate, so they stop dragging the average down, and successive passes
    climb onto the harmonic peaks where the vocal tract actually shows itself.
    """
    spec_db, bin_hz = _frame_spectrum_db(frame, sr)
    n_cep = max(8, int(0.5 * sr / max(f0_hz, 40.0)))
    n_cep = min(n_cep, len(spec_db) // 2 - 1)

    env = _lifter(spec_db, n_cep)
    for _ in range(max_iter):
        target = np.maximum(spec_db, env)
        new_env = _lifter(target, n_cep)
        # Convergence measured only where the spectrum sits ABOVE the envelope:
        # that is the set the update actually moves, and averaging over the
        # valleys would report convergence while the peaks were still climbing.
        above = spec_db > env
        delta = np.max(np.abs(new_env - env)[above]) if np.any(above) else 0.0
        env = new_env
        if delta < tol_db:
            break
    return np.interp(freqs, bin_hz, env)


def _peak_interp_envelope(harmonics: HarmonicModel,
                          freqs: np.ndarray) -> np.ndarray | None:
    """Monotone cubic through the measured harmonic peaks.

    PCHIP, not a natural cubic spline: a spline overshoots between widely spaced
    knots, and at high f0 the knots ARE widely spaced. An overshoot invents a
    formant that is not there, which after transposition is audible as a
    resonance the singer never had.

    Interpolation is done in LOG frequency because formant spacing is roughly
    logarithmic; in linear frequency the low formants — the ones that carry
    vowel identity — get the fewest effective knots.
    """
    if harmonics.n_harmonics < 3:
        return None
    hz = np.asarray(harmonics.frequency_hz, dtype=float)
    db = np.asarray(harmonics.amplitude_db, dtype=float)
    keep = hz > 0
    hz, db = hz[keep], db[keep]
    if hz.size < 3:
        return None

    order = np.argsort(hz)
    hz, db = hz[order], db[order]
    interp = PchipInterpolator(np.log(hz), db, extrapolate=False)
    env = interp(np.log(np.maximum(freqs, 1.0)))
    # HOLD, do not extrapolate. Beyond the observed harmonics there is no
    # evidence, and a cubic run off the end of its knots produces tens of dB of
    # confident nonsense.
    env[freqs < hz[0]] = db[0]
    env[freqs > hz[-1]] = db[-1]
    return env


# =============================================================================
#  Public API
# =============================================================================
METHODS = ("lpc", "cepstral", "true", "peak")


def estimate_envelope(audio: np.ndarray, sr: int, pitch: PitchTrack,
                      start_s: float, end_s: float, method: str,
                      freqs: np.ndarray = DEFAULT_FREQS,
                      harmonics: HarmonicModel | None = None,
                      lpc_order: int = 0,
                      lpc_pre_emphasis: float = 0.5) -> SpectralEnvelope:
    """One envelope over a stable region, medianed across frames."""
    result = SpectralEnvelope(method=method,
                              frequency_hz=freqs.tolist())

    if method == "peak":
        if harmonics is None:
            result.notes = "peak interpolation needs a harmonic model"
            return result
        env = _peak_interp_envelope(harmonics, freqs)
        if env is None:
            result.notes = "fewer than 3 harmonics; nothing to interpolate"
            return result
        result.magnitude_db = env.tolist()
        result.confidence = (Confidence.DETECTED if harmonics.n_harmonics >= 8
                             else Confidence.LOW)
        result.notes = f"{harmonics.n_harmonics} knots"
        return result

    pitch_times = np.asarray(pitch.times_s)
    pitch_hz = np.asarray(pitch.frequency_hz)
    hop = max(1, int(HOP_S * sr))
    first = int(start_s * sr)
    last = min(int(end_s * sr), audio.size) - N_FFT
    if last <= first or pitch_times.size < 2:
        result.notes = "region too short"
        return result

    frames = []
    for pos in range(first, last, hop):
        f0 = float(np.interp((pos + N_FFT * 0.5) / sr, pitch_times, pitch_hz))
        if f0 <= 0:
            continue
        chunk = audio[pos:pos + N_FFT]

        if method == "lpc":
            env = _lpc_envelope(chunk, sr, freqs, order=lpc_order, f0_hz=f0,
                                pre_emphasis=lpc_pre_emphasis)
        elif method == "cepstral":
            env = _cepstral_envelope(chunk, sr, freqs, f0)
        elif method == "true":
            env = _true_envelope(chunk, sr, freqs, f0)
        else:
            raise ValueError(f"unknown envelope method: {method}")
        if env is not None and np.all(np.isfinite(env)):
            frames.append(env)

    if not frames:
        result.notes = "no frame produced an envelope"
        return result

    stacked = np.stack(frames)
    result.magnitude_db = np.median(stacked, axis=0).tolist()
    # Frame-to-frame disagreement is the honest confidence signal: an envelope
    # that reads the same every frame is measured, one that jumps is not.
    spread = float(np.median(np.std(stacked, axis=0)))
    result.confidence = (Confidence.DETECTED if spread < 3.0
                         else Confidence.ESTIMATED if spread < 8.0
                         else Confidence.LOW)
    result.notes = f"{len(frames)} frames, median frame spread {spread:.1f} dB"
    return result


def envelope_at(envelope: SpectralEnvelope, freq_hz) -> np.ndarray:
    """Sample a stored envelope at arbitrary frequencies."""
    return np.interp(np.asarray(freq_hz, dtype=float),
                     np.asarray(envelope.frequency_hz),
                     np.asarray(envelope.magnitude_db))


def find_formant_peaks(envelope: SpectralEnvelope, max_peaks: int = 4,
                       min_hz: float = 200.0, max_hz: float = 4000.0):
    """Local maxima of the envelope, strongest first, returned in Hz order."""
    hz = np.asarray(envelope.frequency_hz)
    db = np.asarray(envelope.magnitude_db)
    if db.size == 0:
        return []
    window = (hz >= min_hz) & (hz <= max_hz)
    idx, props = scipy.signal.find_peaks(db[window], prominence=1.0)
    if idx.size == 0:
        return []
    strongest = np.argsort(props["prominences"])[::-1][:max_peaks]
    return sorted(float(hz[window][idx][i]) for i in strongest)
