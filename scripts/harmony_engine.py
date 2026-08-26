"""
HarmonyEngine — scale-aware multi-voice harmony for Sattari Auto Pitch.

Musical rules:
- Each voice produces a diatonic interval above/below the melody note
- Voice stays in key/scale — chromatic passing tones in melody → snap to scale
- Voice leading: prefer minimal motion between successive frames
- No illegal scale tones (chromatic only when scale=chromatic)
- Supports key C..B, scale major/minor/chromatic
- Supported intervals: +3rd, -3rd, +5th, -5th, +oct, -oct, +6th, -6th, unison
"""

# Scale degrees in semitones (0=root)
SCALE_SEMITONES = {
    "major":     [0, 2, 4, 5, 7, 9, 11],
    "minor":     [0, 2, 3, 5, 7, 8, 10],   # natural minor
    "chromatic": list(range(12)),
}

ROOT_NAMES = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3,
              "E": 4, "F": 5, "F#": 6, "Gb": 6, "G": 7, "G#": 8,
              "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11}

# Diatonic interval lookup table (degrees to add)
INTERVAL_DEGREES = {
    "+3rd": +2,   # 2 diatonic steps up = diatonic 3rd
    "-3rd": -2,
    "+5th": +4,   # 4 diatonic steps up = diatonic 5th
    "-5th": -4,
    "+6th": +5,
    "-6th": -5,
    "+oct": +7,
    "-oct": -7,
    "unison": 0,
}


def build_scale_notes(root_midi, scale):
    """Return all MIDI notes in the given key/scale within a ±3 octave range."""
    semitones = SCALE_SEMITONES[scale]
    root_pc = root_midi % 12
    notes = set()
    for octave in range(-3, 4):
        for st in semitones:
            n = root_pc + st + (4 + octave) * 12  # center around octave 4
            if 0 <= n <= 127:
                notes.add(n)
    return sorted(notes)


def nearest_scale_note(midi, scale_notes):
    """Return the nearest MIDI note in scale_notes to the given midi float."""
    if not scale_notes:
        return int(round(midi))
    return min(scale_notes, key=lambda n: abs(n - midi))


def diatonic_target(melody_midi, interval_str, root_semitone, scale, prefer_above=True):
    """
    Compute the diatonic harmony target for a melody note.

    Returns: MIDI integer (nearest scale note at requested diatonic interval).
    """
    scale_st = SCALE_SEMITONES[scale]
    n_degrees = len(scale_st)

    # Find the melody's scale degree (or nearest in-scale note)
    scale_notes = build_scale_notes(root_semitone, scale)
    melody_snapped = nearest_scale_note(melody_midi, scale_notes)

    # Determine position in scale
    pc = (melody_snapped - root_semitone) % 12
    try:
        degree = scale_st.index(pc)
    except ValueError:
        # melody note not in scale (chromatic) — find nearest degree
        degree = min(range(n_degrees), key=lambda d: abs(scale_st[d] - pc))

    # Compute target degree
    delta_deg = INTERVAL_DEGREES.get(interval_str, 0)
    target_deg = degree + delta_deg

    # Wrap into octave
    octave_shift = target_deg // n_degrees
    wrapped_deg = target_deg % n_degrees
    target_semitone = scale_st[wrapped_deg]

    # Build target MIDI
    base_octave = (melody_snapped - root_semitone) // 12
    target_midi = root_semitone + (base_octave + octave_shift) * 12 + target_semitone

    # Keep in valid MIDI range
    target_midi = max(0, min(127, target_midi))
    return target_midi


class HarmonyVoice:
    def __init__(self, interval, level=1.0, pan=0.0, enabled=True):
        self.interval = interval    # e.g. "+3rd"
        self.level    = level
        self.pan      = pan
        self.enabled  = enabled
        self._last_target = None

    def target(self, melody_midi, root_semitone, scale):
        if not self.enabled:
            return None
        t = diatonic_target(melody_midi, self.interval, root_semitone, scale)
        self._last_target = t
        return t


class HarmonyEngine:
    def __init__(self, key="A", scale="minor", voices=None):
        """
        key:   root note name (C, C#, ..., B)
        scale: major / minor / chromatic
        voices: list of HarmonyVoice or interval strings
        """
        if key not in ROOT_NAMES:
            raise ValueError(f"Unknown key: {key}")
        if scale not in SCALE_SEMITONES:
            raise ValueError(f"Unknown scale: {scale}")
        self.root = ROOT_NAMES[key]
        self.scale = scale
        self._scale_notes = build_scale_notes(self.root, scale)

        if voices is None:
            voices = ["+3rd", "-3rd", "+5th"]
        self.voices = []
        for v in voices:
            if isinstance(v, str):
                self.voices.append(HarmonyVoice(v))
            else:
                self.voices.append(v)

    def set_key(self, key, scale=None):
        self.root = ROOT_NAMES[key]
        if scale:
            self.scale = scale
        self._scale_notes = build_scale_notes(self.root, self.scale)

    def snap_to_scale(self, midi):
        """Return nearest in-scale MIDI note."""
        return nearest_scale_note(midi, self._scale_notes)

    def target(self, melody_midi, prev_melody_midi=None):
        """
        Compute all voice targets for a melody note.

        Returns: list of (interval_str, target_midi | None)
        """
        results = []
        for voice in self.voices:
            t = voice.target(melody_midi, self.root, self.scale)
            results.append((voice.interval, t))
        return results

    def is_in_scale(self, midi):
        return int(round(midi)) in self._scale_notes

    def illegal_scale_tones(self, voice_targets):
        """Count targets that are not in the current scale."""
        return sum(1 for _, t in voice_targets if t is not None and not self.is_in_scale(t))
