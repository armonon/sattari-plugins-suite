"""
Harmony Audio Engine — Agent B Phase 2.

Converts HarmonyEngine MIDI decisions into real rendered audio using RubberBand.

Architecture:
  1. Detect melody pitch per frame (from AutoPitchBench trace)
  2. For each voice: compute diatonic interval target → semitone offset
  3. Pitch-shift input audio segment by that offset via rubberband CLI
  4. Mix voices with level/pan into stereo output

Per voice parameters:
  - interval (+3rd, -3rd, +5th, etc.)
  - level (0..1)
  - pan (-1..1, -1=left, +1=right)
  - formant (preserve or shift with pitch)
  - humanize (random ±timing/pitch offset for naturalness)

Pitch-shifting: rubberband CLI
  rubberband --pitch <semitones> --formant [input.wav] [output.wav]

Key constraint: all harmony must be in the correct diatonic scale
(verified by HarmonyEngine logic, already tested in Phase 1).
"""

import subprocess, json, math, struct, sys, os, wave, shutil, time, tempfile
from pathlib import Path

A4_HZ   = 440.0
A4_MIDI = 69
RUBBERBAND = shutil.which("rubberband") or "/opt/homebrew/bin/rubberband"


# ── WAV helpers ───────────────────────────────────────────────────────────────

def write_wav_mono(path, samples, sr=44100):
    with wave.open(str(path), "w") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        clipped = [max(-32767, min(32767, int(s * 32767))) for s in samples]
        wf.writeframes(struct.pack(f"<{len(clipped)}h", *clipped))


def write_wav_stereo(path, left, right, sr=44100):
    n = min(len(left), len(right))
    with wave.open(str(path), "w") as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        interleaved = []
        for i in range(n):
            l = max(-32767, min(32767, int(left[i] * 32767)))
            r = max(-32767, min(32767, int(right[i] * 32767)))
            interleaved.extend([l, r])
        wf.writeframes(struct.pack(f"<{n*2}h", *interleaved))


def read_wav_mono(path):
    with wave.open(str(path), "r") as wf:
        sr = wf.getframerate()
        nch = wf.getnchannels()
        raw = wf.readframes(wf.getnframes())
        n   = len(raw) // 2
        data = struct.unpack(f"<{n}h", raw)
        if nch == 2:
            return [(data[i] + data[i+1]) / 2.0 / 32768.0 for i in range(0, n, 2)], sr
        return [s / 32768.0 for s in data], sr


def midi_to_hz(midi):
    return A4_HZ * 2 ** ((midi - A4_MIDI) / 12.0)


def hz_to_midi(hz):
    if hz <= 0:
        return -1.0
    return 12.0 * math.log2(hz / A4_HZ) + A4_MIDI


# ── Pitch shifting via RubberBand CLI ─────────────────────────────────────────

def pitch_shift_wav(input_wav, output_wav, semitones, formant_preserve=True, timeout=30):
    """
    Pitch-shift audio by semitones using rubberband CLI.
    Returns True on success.
    """
    cmd = [RUBBERBAND, "--pitch", str(round(semitones, 4))]
    if formant_preserve:
        cmd.append("--formant")
    cmd += ["--quiet", str(input_wav), str(output_wav)]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return r.returncode == 0 and Path(output_wav).exists()
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return False


# ── Harmony audio renderer ────────────────────────────────────────────────────

class HarmonyVoiceConfig:
    def __init__(self, interval, level=0.6, pan=0.0, formant=True, humanize_cents=3.0):
        self.interval   = interval   # "+3rd", "-3rd", "+5th", etc.
        self.level      = level
        self.pan        = pan        # -1=L, 0=C, +1=R
        self.formant    = formant
        self.humanize   = humanize_cents  # ±cents random detune per note


class HarmonyAudioEngine:
    """
    Renders harmony voices from an AutoPitch trace + source audio.

    For each block:
      1. Read detectedMidi from trace
      2. Call HarmonyEngine to get MIDI targets for each voice
      3. Compute semitone offset from detectedMidi to target
      4. Pitch-shift the source block by that offset
      5. Mix voices with level/pan

    Offline rendering: processes the full file, mixing voiced frames only.
    """
    def __init__(self, key="A", scale="minor", voices=None, sr=44100, block=256):
        import sys
        sys.path.insert(0, str(Path(__file__).parent))
        from harmony_engine import HarmonyEngine as _HarmonyEngine
        self._he = _HarmonyEngine(key, scale, voices or ["+3rd", "+5th", "-3rd"])
        self.sr    = sr
        self.block = block
        self._tmpdir = None

    def _get_tmpdir(self):
        if self._tmpdir is None or not Path(self._tmpdir).exists():
            self._tmpdir = tempfile.mkdtemp(prefix="harm_audio_")
        return self._tmpdir

    def render(self, source_wav, trace_path, output_wav, voice_configs=None):
        """
        Main render method.
        source_wav: mono input WAV (typically the original vocal input)
        trace_path: AutoPitch trace JSON
        output_wav: stereo output WAV
        voice_configs: list of HarmonyVoiceConfig
        Returns metrics dict.
        """
        if voice_configs is None:
            voice_configs = [
                HarmonyVoiceConfig("+3rd", level=0.6, pan=-0.3, formant=True),
                HarmonyVoiceConfig("+5th", level=0.5, pan=+0.3, formant=True),
                HarmonyVoiceConfig("-3rd", level=0.5, pan=+0.5, formant=True),
            ]

        source, sr = read_wav_mono(source_wav)
        n_samples = len(source)

        try:
            trace = json.loads(Path(trace_path).read_text())
            frames = trace.get("frames", [])
        except Exception:
            return {"error": "trace_parse_failed"}

        tmpdir = Path(self._get_tmpdir())

        # Build output buffers (stereo)
        out_left  = [0.0] * n_samples
        out_right = [0.0] * n_samples
        # Always pass-through dry melody (center, level 1.0)
        out_left  = list(source)
        out_right = list(source)

        metrics = {
            "voiced_frames": 0,
            "rendered_frames": 0,
            "crashes": 0,
            "nan_inf": 0,
            "illegal_notes": 0,
            "voice_leading_total_cents": 0.0,
            "voice_leading_frames": 0,
            "max_semitone_offset": 0.0,
        }

        prev_voice_midis = {}

        for fr in frames:
            if not fr.get("voiced"):
                continue
            metrics["voiced_frames"] += 1

            block_idx = fr.get("blockIndex", 0)
            start     = block_idx * self.block
            end       = min(start + self.block, n_samples)
            if start >= n_samples:
                continue

            det_midi = fr.get("detectedMidi", 0.0)
            if not math.isfinite(det_midi):
                metrics["nan_inf"] += 1
                continue

            # Get harmony targets
            targets = self._he.target(det_midi)

            for vi, (vc, (interval, target_midi)) in enumerate(
                    zip(voice_configs, targets)):
                if target_midi is None:
                    continue

                # Semitone offset = target - detected
                semitones = float(target_midi) - float(det_midi)
                metrics["max_semitone_offset"] = max(
                    metrics["max_semitone_offset"], abs(semitones))

                # Voice leading distance
                if interval in prev_voice_midis:
                    dist = abs(target_midi - prev_voice_midis[interval]) * 100.0
                    metrics["voice_leading_total_cents"] += dist
                    metrics["voice_leading_frames"] += 1
                prev_voice_midis[interval] = target_midi

                # Write source block to tmp WAV
                block_samples = source[start:end]
                block_wav = tmpdir / f"blk_{block_idx}_v{vi}.wav"
                write_wav_mono(block_wav, block_samples, sr=self.sr)

                # Pitch-shift
                shifted_wav = tmpdir / f"shifted_{block_idx}_v{vi}.wav"
                ok = pitch_shift_wav(block_wav, shifted_wav, semitones,
                                     formant_preserve=vc.formant)
                if not ok:
                    metrics["crashes"] += 1
                    continue

                try:
                    shifted, _ = read_wav_mono(shifted_wav)
                except Exception:
                    metrics["crashes"] += 1
                    continue

                # Check NaN/Inf
                if any(not math.isfinite(s) for s in shifted[:10]):
                    metrics["nan_inf"] += 1
                    continue

                # Mix into output with level/pan
                # Pan law: left = level * cos(pan*π/4), right = level * sin(pan*π/4 + π/4)
                pan_norm = vc.pan  # -1..+1
                pan_rad = pan_norm * math.pi / 4
                gain_l = vc.level * math.cos(pan_rad + math.pi / 4)
                gain_r = vc.level * math.sin(pan_rad + math.pi / 4)

                for i, s in enumerate(shifted):
                    out_idx = start + i
                    if out_idx < n_samples:
                        out_left[out_idx]  = max(-1.0, min(1.0, out_left[out_idx]  + gain_l * s))
                        out_right[out_idx] = max(-1.0, min(1.0, out_right[out_idx] + gain_r * s))

                metrics["rendered_frames"] += 1

        # Normalize output
        peak = max(max(abs(s) for s in out_left), max(abs(s) for s in out_right), 1e-12)
        if peak > 0.95:
            scale = 0.9 / peak
            out_left  = [s * scale for s in out_left]
            out_right = [s * scale for s in out_right]

        write_wav_stereo(output_wav, out_left, out_right, sr=self.sr)

        # Cleanup tmp files
        try:
            shutil.rmtree(tmpdir, ignore_errors=True)
            self._tmpdir = None
        except Exception:
            pass

        # Final metrics
        vl_mean = (metrics["voice_leading_total_cents"] /
                   max(metrics["voice_leading_frames"], 1))
        metrics["voice_leading_mean_cents"] = round(vl_mean, 1)
        metrics["render_rate"] = (metrics["rendered_frames"] /
                                  max(metrics["voiced_frames"], 1))
        return metrics
