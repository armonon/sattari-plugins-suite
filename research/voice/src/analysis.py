"""Stages 1-3: preprocessing, stable-region selection, F0, harmonic extraction.

These are the stages that are INDEPENDENT of the spectral-envelope method still
under comparison, so they can be built and validated before that decision.

Everything here returns confidence alongside its estimate, and every function is
allowed to decline. A stage that always produces numbers cannot be distinguished
from one that guesses.
"""
from __future__ import annotations

import numpy as np
import librosa
import scipy.signal

from model import (Confidence, HarmonicModel, PitchTrack, SourceMetadata,
                   StableRegion)

# Control-rate feature hop. 5 ms / 200 Hz, per the modulation-analysis spec:
# fast enough to resolve a 20 Hz tremolo with ten samples per cycle.
HOP_S = 0.005


# =============================================================================
#  Stage 1 — preprocessing
# =============================================================================
def load_audio(path: str, target_sr: int = 48000) -> tuple[np.ndarray, SourceMetadata]:
    """Load, mono, resample, DC-remove, gentle high-pass.

    The high-pass is CONSERVATIVE (40 Hz) on purpose. Anything more aggressive
    starts removing the first harmonic of a low male voice, which then corrupts
    both F0 confidence and the spectral tilt estimate.
    """
    import soundfile as sf

    audio, sr = sf.read(path, always_2d=True, dtype="float32")
    channels = audio.shape[1]
    mono = audio.mean(axis=1)

    if sr != target_sr:
        mono = librosa.resample(mono, orig_sr=sr, target_sr=target_sr)

    peak = float(np.max(np.abs(mono))) if mono.size else 0.0
    mono = mono - float(np.mean(mono))                      # DC
    sos = scipy.signal.butter(2, 40.0, "highpass", fs=target_sr, output="sos")
    mono = scipy.signal.sosfiltfilt(sos, mono).astype(np.float32)

    meta = SourceMetadata(
        filename=path,
        sample_rate=target_sr,
        duration_s=len(mono) / target_sr,
        channels=channels,
        peak_dbfs=20.0 * np.log10(max(peak, 1e-9)),
    )
    return mono, meta


def frame_features(audio: np.ndarray, sr: int) -> dict[str, np.ndarray]:
    """Control-rate curves that region selection and modulation analysis share."""
    hop = max(1, int(HOP_S * sr))
    frame = hop * 8                                   # 40 ms analysis window

    rms = librosa.feature.rms(y=audio, frame_length=frame, hop_length=hop)[0]
    flux = librosa.onset.onset_strength(y=audio, sr=sr, hop_length=hop)
    times = librosa.frames_to_time(np.arange(len(rms)), sr=sr, hop_length=hop)

    n = min(len(rms), len(flux), len(times))
    return {
        "times_s": times[:n],
        "rms": rms[:n],
        "rms_db": 20.0 * np.log10(np.maximum(rms[:n], 1e-9)),
        "flux": flux[:n],
        "hop": hop,
    }


# =============================================================================
#  Stage 2 — fundamental frequency
# =============================================================================
def track_f0(audio: np.ndarray, sr: int,
             fmin: float = 65.0, fmax: float = 1000.0) -> PitchTrack:
    """pYIN.

    Chosen because this repo has ALREADY validated pYIN as its gold-standard
    reference during the Auto Pitch work, where CREPE-vs-pYIN disagreement on a
    real breathy voice measured 5-15 cents. That is the accuracy floor for any
    tracker here, and it is already characterised — which is worth more than a
    marginally better tracker with unknown behaviour.
    """
    f0, voiced_flag, voiced_prob = librosa.pyin(
        audio, fmin=fmin, fmax=fmax, sr=sr,
        frame_length=2048, hop_length=max(1, int(HOP_S * sr)))

    times = librosa.times_like(f0, sr=sr, hop_length=max(1, int(HOP_S * sr)))
    f0_clean = np.nan_to_num(f0, nan=0.0)

    track = PitchTrack(
        times_s=times.tolist(),
        frequency_hz=f0_clean.tolist(),
        confidence=np.nan_to_num(voiced_prob, nan=0.0).tolist(),
        voiced=np.nan_to_num(voiced_flag, nan=False).astype(bool).tolist(),
    )

    voiced = f0_clean[(voiced_flag == True) & (f0_clean > 0)]  # noqa: E712
    if voiced.size < 10:
        track.overall_confidence = Confidence.NOT_ENOUGH_AUDIO
        return track

    median = float(np.median(voiced))
    cents = 1200.0 * np.log2(voiced / median)
    track.median_f0_hz = median
    track.cents_std = float(np.std(cents))

    voiced_fraction = float(np.mean(voiced_flag == True))       # noqa: E712
    # Median over VOICED frames only. Averaging across the whole file drags in
    # the release and any silence, and pYIN's voiced_prob is conservative even on
    # a synthetic tone — measured 0.39 mean on a clean 110 Hz vowel that it
    # nonetheless marked 100% voiced. Gating on that mean called perfect input
    # "low confidence".
    conf = np.nan_to_num(voiced_prob, nan=0.0)
    voiced_conf = conf[voiced_flag == True]                     # noqa: E712
    mean_conf = float(np.median(voiced_conf)) if voiced_conf.size else 0.0

    # A sustained note should be stable. Wide cents spread means either vibrato
    # (fine, handled later) or that this is not one sustained note at all — the
    # caller is told which by the region search, not by a silent average.
    if voiced_fraction > 0.6 and mean_conf > 0.25:
        track.overall_confidence = Confidence.DETECTED
    elif voiced_fraction > 0.3:
        track.overall_confidence = Confidence.LOW
    else:
        track.overall_confidence = Confidence.NOT_ENOUGH_AUDIO
    return track


# =============================================================================
#  Stage 1b — stable region
# =============================================================================
def find_stable_region(features: dict, pitch: PitchTrack, sr: int,
                       min_duration_s: float = 0.4) -> StableRegion:
    """Longest span that is loud, voiced, pitch-stable AND spectrally settled.

    ALL FOUR CRITERIA, not just level. A loud region can still be a diphthong
    (tract moving), and a pitch-stable region can still be the attack (spectrum
    moving). Using RMS alone picks the loudest moment, which for many singers is
    partway through the attack — exactly where the tract is least stationary.
    """
    times = np.asarray(features["times_s"])
    rms_db = np.asarray(features["rms_db"])
    flux = np.asarray(features["flux"])

    if times.size < 8:
        return StableRegion(reason="recording too short to analyse")

    pitch_times = np.asarray(pitch.times_s)
    pitch_hz = np.asarray(pitch.frequency_hz)
    voiced = np.asarray(pitch.voiced, dtype=bool)
    if pitch_times.size < 4:
        return StableRegion(reason="no pitch track")

    f0_on_features = np.interp(times, pitch_times, pitch_hz)
    voiced_on_features = np.interp(times, pitch_times, voiced.astype(float)) > 0.5

    loud = rms_db > (np.max(rms_db) - 12.0)
    # RELATIVE TO THE MAXIMUM, NOT A PERCENTILE. A percentile split assumes the
    # distribution has spread; on a genuinely steady tone the flux is near
    # constant (measured: median 0.0217, 60th percentile 0.0231) so a percentile
    # passes frames essentially at random and shatters a perfectly stable
    # 3-second note into 0.14s fragments. The onset is a real outlier — flux
    # peaked at 4.06 against that 0.02 floor — so scaling from the max separates
    # "transient" from "settled" the way the percentile only appeared to.
    flux_max = float(np.max(flux)) if flux.size else 0.0
    settled = flux < max(0.15 * flux_max, 1e-6)

    # Local pitch stability over ~100 ms.
    win = max(3, int(0.1 / HOP_S))
    cents = np.full_like(f0_on_features, np.nan, dtype=float)
    positive = f0_on_features > 0
    cents[positive] = 1200.0 * np.log2(f0_on_features[positive] / np.median(f0_on_features[positive]))
    local_std = np.array([
        np.nanstd(cents[max(0, i - win): i + win + 1]) if positive[i] else 1e9
        for i in range(len(cents))
    ])
    # 120 cents allows a wide natural vibrato through while rejecting a glide.
    stable = local_std < 120.0

    usable = loud & settled & voiced_on_features & stable

    # MORPHOLOGICAL CLOSING. Vibrato makes the spectrum move continuously, so
    # flux oscillates AT THE VIBRATO RATE and ~11% of frames fail "settled" —
    # scattered through the note rather than clustered. Measured on a 5.5 Hz
    # vibrato: 536 of 601 frames pass, yet the longest unbroken run was 0.17s,
    # because the failures punch holes every vibrato peak. Filling gaps shorter
    # than a vibrato half-cycle recovers the region without admitting a real
    # transient, which lasts far longer than 25 frames.
    max_gap = int(0.125 / HOP_S)          # 125 ms ~ half a slow vibrato cycle
    closed = usable.copy()
    gap_start = None
    for i, ok in enumerate(usable):
        if not ok:
            if gap_start is None:
                gap_start = i
        else:
            if gap_start is not None and (i - gap_start) <= max_gap and gap_start > 0:
                closed[gap_start:i] = True
            gap_start = None
    usable = closed

    if not np.any(usable):
        return StableRegion(reason="no region was simultaneously loud, voiced, "
                                   "settled and pitch-stable")

    # Longest run of usable frames.
    best_start = best_len = run_start = run_len = 0
    for i, ok in enumerate(usable):
        if ok:
            if run_len == 0:
                run_start = i
            run_len += 1
            if run_len > best_len:
                best_len, best_start = run_len, run_start
        else:
            run_len = 0

    start_s = float(times[best_start])
    end_s = float(times[min(best_start + best_len - 1, len(times) - 1)])
    duration = end_s - start_s

    if duration < min_duration_s:
        return StableRegion(start_s=start_s, end_s=end_s,
                            confidence=Confidence.NOT_ENOUGH_AUDIO,
                            reason=f"longest stable region {duration:.2f}s < "
                                   f"{min_duration_s:.2f}s minimum")

    confidence = Confidence.DETECTED if duration > 1.0 else Confidence.ESTIMATED
    return StableRegion(start_s=start_s, end_s=end_s, confidence=confidence,
                        reason=f"{duration:.2f}s stable sustain")


# =============================================================================
#  Stage 3 — harmonic partials
# =============================================================================
def _parabolic_peak(mag_db: np.ndarray, k: int) -> tuple[float, float]:
    """Sub-bin peak position and height from a 3-point parabola in dB.

    THIS IS NOT OPTIONAL. Reading the raw bin maximum quantises every harmonic to
    the FFT grid, which at 2048/48k is 23 Hz — enough to misplace a harmonic by a
    quarter-tone and, worse, to under-read its amplitude by several dB depending
    on where it happened to fall between bins. Those errors then propagate into
    the spectral envelope as a ripple that looks like real vocal structure.
    """
    if k <= 0 or k >= len(mag_db) - 1:
        return float(k), float(mag_db[k])
    a, b, c = mag_db[k - 1], mag_db[k], mag_db[k + 1]
    denom = a - 2.0 * b + c
    if abs(denom) < 1e-12:
        return float(k), float(b)
    offset = 0.5 * (a - c) / denom
    offset = float(np.clip(offset, -0.5, 0.5))
    return k + offset, float(b - 0.25 * (a - c) * offset)


def extract_harmonics(audio: np.ndarray, sr: int, f0_hz: float,
                      n_fft: int = 2048,
                      max_harmonics: int = 100,
                      snr_floor_db: float = 12.0) -> HarmonicModel:
    """Single-frame harmonic measurement. Prefer extract_harmonics_multi().

    The window is 2048 (43 ms at 48 kHz), NOT the 8192 this started with. The
    long window resolves harmonics beautifully on a steady tone and falls apart
    on a real one: 8192 samples is 171 ms, a 5.5 Hz vibrato has a 182 ms period,
    so the window spans 0.94 of a cycle and every harmonic SMEARS across the
    range it swept. Measured cost: 7.3 dB amplitude error and 44 cents of
    frequency error against 0.18 dB / 0.3 cents on the same vowel held steady.

    2048 is the compromise: >= 4 periods of a 93 Hz fundamental (enough to
    resolve harmonics) while short enough that pitch is quasi-stationary within
    it for any plausible vibrato.
    """
    model = HarmonicModel(f0_hz=float(f0_hz))
    if f0_hz <= 0 or audio.size < 256:
        return model

    while n_fft > 512 and audio.size < n_fft:
        n_fft //= 2

    window = np.hanning(n_fft)
    segment = np.zeros(n_fft, dtype=float)
    take = min(n_fft, audio.size)
    segment[:take] = audio[:take]
    segment *= window

    spectrum = np.fft.rfft(segment)
    mag = np.abs(spectrum)
    mag_db = 20.0 * np.log10(np.maximum(mag, 1e-12))
    phase = np.angle(spectrum)
    bin_hz = sr / n_fft
    noise_floor_db = float(np.median(mag_db))

    nyquist = sr * 0.5
    max_k = min(max_harmonics, int((nyquist * 0.95) / f0_hz))
    search = max(1, int((f0_hz * 0.4) / bin_hz))

    reliable_to = 0.0
    for k in range(1, max_k + 1):
        centre_bin = int(round((f0_hz * k) / bin_hz))
        lo = max(1, centre_bin - search)
        hi = min(len(mag_db) - 2, centre_bin + search)
        if lo >= hi:
            break

        local = int(lo + np.argmax(mag_db[lo:hi + 1]))
        peak_bin, peak_db = _parabolic_peak(mag_db, local)
        peak_hz = peak_bin * bin_hz

        snr = peak_db - noise_floor_db
        model.harmonic_index.append(k)
        model.frequency_hz.append(float(peak_hz))
        model.amplitude_db.append(float(peak_db))
        model.phase_rad.append(float(phase[local]))
        model.confidence.append(float(np.clip(snr / 30.0, 0.0, 1.0)))
        if snr >= snr_floor_db:
            reliable_to = float(peak_hz)

    if model.amplitude_db:
        peak = max(model.amplitude_db)
        model.amplitude_db = [a - peak for a in model.amplitude_db]
    model.n_harmonics = len(model.harmonic_index)
    model.reliable_up_to_hz = reliable_to
    return model


def extract_harmonics_multi(audio: np.ndarray, sr: int, pitch: PitchTrack,
                            start_s: float, end_s: float,
                            n_fft: int = 2048,
                            hop_s: float = 0.02,
                            max_harmonics: int = 100,
                            snr_floor_db: float = 12.0) -> HarmonicModel:
    """Harmonic amplitudes averaged over MANY short frames. Use this one.

    Two things this does that a single window cannot:

    1. PER-FRAME F0. Each frame is analysed at the pitch it actually had, so a
       vibrato'd note is measured at its instantaneous frequency instead of
       being smeared across the range it swept.

    2. MEDIAN ACROSS FRAMES. The vocal tract is what we want; the vibrato is
       what we want to remove. Taking the median over a whole stable region
       averages the modulation out while being robust to the occasional frame
       that landed on a glitch — which a mean is not.

    The result is the STATIC model the spec asks for: modulation is measured
    separately and reapplied at runtime, so it must not be baked in here.
    """
    model = HarmonicModel()
    if audio.size < 512 or end_s <= start_s:
        return model

    pitch_times = np.asarray(pitch.times_s)
    pitch_hz = np.asarray(pitch.frequency_hz)
    if pitch_times.size < 2:
        return model

    hop = max(1, int(hop_s * sr))
    first = int(start_s * sr)
    last = min(int(end_s * sr), audio.size) - n_fft
    if last <= first:
        return model

    per_frame_db: list[np.ndarray] = []
    per_frame_hz: list[np.ndarray] = []
    anchor_phase: np.ndarray | None = None
    f0s: list[float] = []

    for pos in range(first, last, hop):
        centre_s = (pos + n_fft * 0.5) / sr
        f0 = float(np.interp(centre_s, pitch_times, pitch_hz))
        if f0 <= 0:
            continue

        frame = extract_harmonics(audio[pos:pos + n_fft], sr, f0,
                                  n_fft=n_fft, max_harmonics=max_harmonics,
                                  snr_floor_db=snr_floor_db)
        if frame.n_harmonics == 0:
            continue

        f0s.append(f0)
        per_frame_db.append(np.asarray(frame.amplitude_db))
        # Ratios, not absolute Hz: the whole point is that the tract stays put
        # while the harmonics move, so harmonic position is stored relative to
        # the f0 THIS frame actually had.
        per_frame_hz.append(np.asarray(frame.frequency_hz) / f0)
        if anchor_phase is None:
            anchor_phase = np.asarray(frame.phase_rad)

    if not per_frame_db:
        return model

    width = min(len(a) for a in per_frame_db)
    stacked_db = np.stack([a[:width] for a in per_frame_db])
    stacked_ratio = np.stack([a[:width] for a in per_frame_hz])

    median_f0 = float(np.median(f0s))
    model.f0_hz = median_f0
    model.harmonic_index = list(range(1, width + 1))
    model.amplitude_db = np.median(stacked_db, axis=0).tolist()
    model.frequency_hz = (np.median(stacked_ratio, axis=0) * median_f0).tolist()
    model.phase_rad = (anchor_phase[:width].tolist()
                       if anchor_phase is not None else [0.0] * width)

    # Frame-to-frame amplitude spread IS the confidence: a harmonic that reads
    # the same in every frame is measured, one that jumps around is not.
    spread_db = np.std(stacked_db, axis=0)
    model.confidence = np.clip(1.0 - spread_db / 12.0, 0.0, 1.0).tolist()

    peak = max(model.amplitude_db)
    model.amplitude_db = [a - peak for a in model.amplitude_db]
    model.n_harmonics = width

    reliable = [f for f, a in zip(model.frequency_hz, model.amplitude_db)
                if a > -60.0]
    model.reliable_up_to_hz = float(max(reliable)) if reliable else 0.0
    return model
