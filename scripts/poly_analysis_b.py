"""
Polyphonic Multi-F0 Analyzer — Phase B

Primary method: Spectral Salience (librosa.salience)
  - Accumulates energy at harmonics 1-6 INTO the fundamental frequency.
  - Peaks in salience = fundamental frequencies, not overtones.
  - Temporal average over full signal for stability.

Harmonic suppression:
  - Confirms notes sorted by salience descending.
  - Suppresses note H if it is approximately the Nth harmonic of a stronger note L
    (ratio close to 2, 3, 4, 5) AND conf(H) < conf(L) × SUPPRESS_RATIO.
  - Preserves real octave doublings: genuine C3+C4 have near-equal salience,
    so C4 is not suppressed even though it's C3's 2nd harmonic.

Frequency resolution: n_fft=8192 → bin width 5.4 Hz at 44100 Hz.
Bass notes < 200 Hz are resolved at ~0.5 semitone accuracy.
"""

import numpy as np
import math

A4_HZ = 440.0
A4_MIDI = 69

SUPPRESS_RATIO = 0.40   # suppress H if salience(H) < salience(L) × this
HARMONIC_TOL   = 0.08   # Hz-ratio tolerance around integer (8%)
SAL_HARMONICS  = [1, 2, 3, 4, 5, 6]
SAL_WEIGHTS    = [1.0, 0.70, 0.50, 0.35, 0.25, 0.18]


def hz_to_midi(hz):
    if hz <= 0:
        return -1.0
    return 12.0 * math.log2(hz / A4_HZ) + A4_MIDI


def midi_to_hz(midi):
    return A4_HZ * 2 ** ((midi - A4_MIDI) / 12.0)


def make_poly_signal(midi_notes, sr=44100, dur=2.0, amp_per_note=0.4):
    """Synthesize a polyphonic tone (fundamental + 2 harmonics per note)."""
    n = int(sr * dur)
    t = np.arange(n) / sr
    sig = np.zeros(n)
    for midi in midi_notes:
        hz = midi_to_hz(midi)
        sig += amp_per_note * np.sin(2 * np.pi * hz * t)
        sig += amp_per_note * 0.4 * np.sin(2 * np.pi * 2 * hz * t)
        sig += amp_per_note * 0.2 * np.sin(2 * np.pi * 3 * hz * t)
    peak = np.max(np.abs(sig))
    if peak > 0:
        sig *= 0.8 / peak
    return sig


class PolyAnalyzerB:
    """
    Phase B polyphonic analyzer.
    Improvements over Phase A:
      - Spectral salience collapses harmonics into fundamentals → better precision
      - Salience averaged over full signal → better recall for weak voices
      - Harmonic suppression with salience-ratio test
      - n_fft=8192 for better low-frequency resolution
    """

    def __init__(self, sr=44100, n_fft=8192, hop=1024,
                 max_voices=6, min_hz=80.0, max_hz=2000.0,
                 conf_threshold=0.04,
                 cluster_semitones=0.5):
        self.sr = sr
        self.n_fft = n_fft
        self.hop = hop
        self.max_voices = max_voices
        self.min_hz = min_hz
        self.max_hz = max_hz
        self.conf_threshold = conf_threshold
        self.cluster_semitones = cluster_semitones

    def analyze(self, audio):
        """
        Analyze an audio array and return detected fundamental frequencies.

        audio: numpy float32 array, mono
        Returns: list of dicts sorted by salience descending
          [{hz, midi, midi_int, confidence, raw_salience}]
        """
        import librosa
        from scipy.signal import find_peaks

        audio = np.asarray(audio, dtype=np.float32)

        # STFT — larger FFT for better low-freq resolution
        S = np.abs(librosa.stft(audio, n_fft=self.n_fft, hop_length=self.hop))
        freqs = librosa.fft_frequencies(sr=self.sr, n_fft=self.n_fft)

        # Spectral salience: sum weighted energy at harmonics into each bin
        try:
            S_sal = librosa.salience(S, freqs=freqs, harmonics=SAL_HARMONICS,
                                     weights=SAL_WEIGHTS, fill_value=0.0)
        except TypeError:
            # Older librosa: positional args only
            S_sal = librosa.salience(S, freqs, SAL_HARMONICS, SAL_WEIGHTS,
                                     fill_value=0.0)

        # Temporal average → stable estimate of fundamental salience
        sal = np.mean(S_sal, axis=1)

        # Restrict to vocal/instrument range
        min_bin = max(1, int(np.searchsorted(freqs, self.min_hz)))
        max_bin = min(len(freqs) - 1, int(np.searchsorted(freqs, self.max_hz)))
        sal_r = sal[min_bin:max_bin]
        freq_r = freqs[min_bin:max_bin]

        if len(sal_r) == 0 or sal_r.max() == 0:
            return []

        total_sal = float(np.sum(sal_r)) + 1e-12

        # Find local maxima in salience
        peaks, _ = find_peaks(sal_r,
                               height=sal_r.max() * 0.05,
                               distance=2,
                               prominence=sal_r.max() * 0.02)

        # Build candidates sorted by salience descending
        candidates = []
        for p in peaks:
            hz = float(freq_r[p])
            midi_f = hz_to_midi(hz)
            conf = float(sal_r[p]) / total_sal
            raw_sal = float(sal_r[p])
            candidates.append({'hz': round(hz, 2),
                                'midi': round(midi_f, 2),
                                'midi_int': int(round(midi_f)),
                                'confidence': round(conf, 4),
                                'raw_salience': raw_sal})
        candidates.sort(key=lambda x: -x['raw_salience'])

        # Deduplicate + harmonic suppression
        accepted = []
        seen_midis = []

        for note in candidates:
            if note['confidence'] < self.conf_threshold:
                continue

            # Skip if too close in MIDI space to an already-accepted note
            if any(abs(note['midi'] - sm) < self.cluster_semitones
                   for sm in seen_midis):
                continue

            # Harmonic suppression: is this note the Nth harmonic of a stronger note?
            suppressed = False
            for acc in accepted:
                ratio = note['hz'] / (acc['hz'] + 1e-9)
                for n in [2, 3, 4, 5]:
                    if abs(ratio - n) < HARMONIC_TOL:
                        # H is approx the Nth harmonic of L.
                        # Suppress only if H is substantially weaker than L.
                        if note['raw_salience'] < acc['raw_salience'] * SUPPRESS_RATIO:
                            suppressed = True
                            break
                if suppressed:
                    break

            if suppressed:
                continue

            if len(accepted) >= self.max_voices:
                break

            accepted.append(note)
            seen_midis.append(note['midi'])

        return accepted
