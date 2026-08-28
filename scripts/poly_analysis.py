"""
Polyphonic Multi-F0 Analyzer — Phase A
Method: Iterative Harmonic Product Spectrum (HPS) with harmonic subtraction.

Pipeline:
1. FFT frame (4096 samples, Hann window)
2. HPS: multiply spectrum downsampled by 2x, 3x, 4x → peaks = F0 candidates
3. Find strongest F0, compute confidence from harmonic energy ratio
4. Subtract harmonics from spectrum (spectral whitening)
5. Repeat until max_voices reached or energy below threshold
"""

import numpy as np
import math

A4_HZ   = 440.0
A4_MIDI = 69

def hz_to_midi(hz):
    if hz <= 0:
        return -1.0
    return 12.0 * math.log2(hz / A4_HZ) + A4_MIDI

def midi_to_hz(midi):
    return A4_HZ * 2 ** ((midi - A4_MIDI) / 12.0)


class PolyAnalyzer:
    def __init__(self, sr=44100, frame_size=4096, hop=512,
                 max_voices=6, min_hz=80.0, max_hz=2000.0,
                 conf_threshold=0.25):
        self.sr = sr
        self.frame_size = frame_size
        self.hop = hop
        self.max_voices = max_voices
        self.min_hz = min_hz
        self.max_hz = max_hz
        self.conf_threshold = conf_threshold
        self._window = np.hanning(frame_size)
        self.min_bin = max(1, int(min_hz * frame_size / sr))
        self.max_bin = min(frame_size // 2, int(max_hz * frame_size / sr))

    def _hps(self, mag, n_harm=4):
        """Harmonic Product Spectrum — multiply downsampled copies."""
        L = len(mag)
        product = mag[:L // n_harm].copy()
        for h in range(2, n_harm + 1):
            ds = mag[::h][:L // n_harm]
            product[:len(ds)] *= ds
        return product

    def _find_peak(self, mag, min_bin, max_bin):
        """Find strongest bin in range, return (bin_index, value)."""
        region = mag[min_bin:max_bin]
        if len(region) == 0:
            return -1, 0.0
        local_peak = int(np.argmax(region)) + min_bin
        return local_peak, mag[local_peak]

    def _harmonic_energy(self, mag, f0_hz, n_harm=6):
        """Sum energy at f0 harmonics."""
        energy = 0.0
        for h in range(1, n_harm + 1):
            f = f0_hz * h
            if f > self.sr / 2:
                break
            b = int(round(f * self.frame_size / self.sr))
            if 0 <= b < len(mag):
                # Sum ±2 bins around harmonic
                energy += np.sum(mag[max(0, b-2):b+3] ** 2)
        return energy

    def _subtract_harmonics(self, mag, f0_hz, strength=0.85, n_harm=8):
        """Subtract harmonic peaks from spectrum (inplace copy)."""
        out = mag.copy()
        for h in range(1, n_harm + 1):
            f = f0_hz * h
            if f > self.sr / 2:
                break
            b = int(round(f * self.frame_size / self.sr))
            for db in range(-3, 4):
                bi = b + db
                if 0 <= bi < len(out):
                    w = 1.0 - strength * max(0, 1 - abs(db) / 4)
                    out[bi] *= w
        return out

    def analyze_frame_librosa(self, audio_frame):
        """
        Alternative: use librosa piptrack for polyphonic pitch detection.
        Returns list of {hz, midi, midi_int, confidence}.
        """
        try:
            import librosa
            import numpy as np
            if len(audio_frame) < self.frame_size:
                audio_frame = np.pad(audio_frame, (0, self.frame_size - len(audio_frame)))
            frame = audio_frame[:self.frame_size].astype(np.float32)
            pitches, magnitudes = librosa.piptrack(
                y=frame, sr=self.sr,
                n_fft=self.frame_size, hop_length=self.frame_size,
                fmin=self.min_hz, fmax=self.max_hz,
                threshold=0.1,
            )
            # pitches/magnitudes are (freq_bins, 1 frame)
            pitch_col = pitches[:, 0]
            mag_col   = magnitudes[:, 0]

            # Keep all nonzero pitches above threshold, sorted by magnitude
            valid = [(pitch_col[i], mag_col[i]) for i in range(len(pitch_col))
                     if pitch_col[i] > 0 and mag_col[i] > 0]
            valid.sort(key=lambda x: -x[1])

            # Cluster nearby pitches (within 50¢) to avoid duplicates
            total_mag = sum(m for _, m in valid) + 1e-12
            voices = []
            seen_midis = []
            for hz, mag in valid:
                midi_f = hz_to_midi(hz)
                if any(abs(midi_f - sm) < 0.5 for sm in seen_midis):
                    continue
                conf = mag / total_mag
                if conf < self.conf_threshold:
                    continue
                if len(voices) >= self.max_voices:
                    break
                seen_midis.append(midi_f)
                voices.append({
                    "hz":         round(hz, 2),
                    "midi":       round(midi_f, 2),
                    "midi_int":   int(round(midi_f)),
                    "confidence": round(conf, 4),
                })
            return voices
        except ImportError:
            return self.analyze_frame(audio_frame)

    def analyze_frame(self, audio_frame):
        """
        Analyze one audio frame.
        Returns: list of dicts [{hz, midi, confidence}]
        """
        if len(audio_frame) < self.frame_size:
            audio_frame = np.pad(audio_frame, (0, self.frame_size - len(audio_frame)))
        frame = audio_frame[:self.frame_size] * self._window
        fft = np.abs(np.fft.rfft(frame))
        mag = fft

        total_energy = np.sum(mag ** 2) + 1e-12
        voices = []
        current_mag = mag.copy()

        for _ in range(self.max_voices):
            hps = self._hps(current_mag)
            min_b = max(1, int(self.min_hz * self.frame_size / self.sr))
            max_b = min(len(hps), int(self.max_hz * self.frame_size / self.sr // 4))
            peak_bin, peak_val = self._find_peak(hps, min_b, max_b)
            if peak_bin < 1:
                break

            f0_hz = peak_bin * self.sr / self.frame_size
            if not (self.min_hz <= f0_hz <= self.max_hz):
                break

            # Confidence: harmonic energy / total energy
            h_energy = self._harmonic_energy(current_mag, f0_hz)
            confidence = h_energy / total_energy

            if confidence < self.conf_threshold:
                break

            voices.append({
                "hz":         round(f0_hz, 2),
                "midi":       round(hz_to_midi(f0_hz), 2),
                "midi_int":   int(round(hz_to_midi(f0_hz))),
                "confidence": round(confidence, 4),
            })

            # Subtract harmonics for next iteration
            current_mag = self._subtract_harmonics(current_mag, f0_hz)

        return voices

    def analyze(self, audio, center_frame=True):
        """
        Analyze audio array.
        If center_frame=True: analyze single center frame.
        Returns list of voice dicts.
        """
        if center_frame:
            start = max(0, len(audio) // 2 - self.frame_size // 2)
            frame = audio[start:start + self.frame_size]
            return self.analyze_frame(frame)
        # Multi-hop analysis: return consensus from all frames
        all_voices = []
        for i in range(0, len(audio) - self.frame_size, self.hop):
            v = self.analyze_frame(audio[i:i + self.frame_size])
            all_voices.append(v)
        return all_voices


def make_poly_signal(midi_notes, sr=44100, dur=2.0, amp_per_note=0.4):
    """Synthesize a clean polyphonic tone (sum of sine waves at given MIDI notes)."""
    n = int(sr * dur)
    t = np.arange(n) / sr
    sig = np.zeros(n)
    for midi in midi_notes:
        hz = midi_to_hz(midi)
        # Add fundamental + 2 harmonics for realism
        sig += amp_per_note * np.sin(2 * np.pi * hz * t)
        sig += amp_per_note * 0.4 * np.sin(2 * np.pi * 2 * hz * t)
        sig += amp_per_note * 0.2 * np.sin(2 * np.pi * 3 * hz * t)
    peak = np.max(np.abs(sig))
    if peak > 0:
        sig *= 0.8 / peak
    return sig
