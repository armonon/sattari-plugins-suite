"""VoiceInstrumentModel — the serialisable result of analysis.

WHY THIS FILE EXISTS FIRST
--------------------------
The model is the contract between analysis and synthesis, and between the Python
research prototype and the eventual C++ runtime. Fixing it early forces every
stage to declare what it actually produces — including what it could not
determine, which is the part that tends to get quietly dropped.

TWO PRINCIPLES, BOTH LOAD-BEARING
---------------------------------
1. EVERY ESTIMATE CARRIES A CONFIDENCE, and every stage may decline. An analyzer
   that always returns numbers is indistinguishable from one that guesses. The
   `Confidence` enum makes "I could not tell" a first-class value rather than a
   silent 0.0 that a caller will treat as data.

2. THE MODEL IS VERSIONED FROM COMMIT ONE. It will change substantially — the
   envelope representation alone has four candidate methods still to be compared
   — and a model file that cannot say which schema produced it is unloadable a
   month later.

WHAT IS DELIBERATELY NOT HERE
-----------------------------
No audio. The model is what you need to PLAY the voice, not the recording. If a
field could be replaced by "keep a copy of the sample", it does not belong here;
that is the line between this instrument and a sampler.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, field, asdict
from enum import Enum
from typing import Any

SCHEMA_VERSION = 1


class Confidence(str, Enum):
    """How much a consumer should trust a field.

    Ordered worst to best. Anything below DETECTED should be surfaced in the UI
    as an estimate, and NOT_ENOUGH_AUDIO should block model creation rather than
    produce a plausible-looking instrument from insufficient evidence.
    """

    NOT_ENOUGH_AUDIO = "not_enough_audio"
    LOW = "low"
    ESTIMATED = "estimated"
    DETECTED = "detected"


@dataclass
class SourceMetadata:
    filename: str = ""
    sample_rate: int = 0
    duration_s: float = 0.0
    channels: int = 0
    peak_dbfs: float = 0.0


@dataclass
class StableRegion:
    """The span the model was built from — NOT the whole recording.

    Analysing everything averages the attack, the sustain and the release into a
    vocal tract that existed at no point in the recording. The sustain is the
    only part where the tract is actually stationary.
    """

    start_s: float = 0.0
    end_s: float = 0.0
    confidence: Confidence = Confidence.NOT_ENOUGH_AUDIO
    reason: str = ""


@dataclass
class PitchTrack:
    times_s: list[float] = field(default_factory=list)
    frequency_hz: list[float] = field(default_factory=list)
    confidence: list[float] = field(default_factory=list)
    voiced: list[bool] = field(default_factory=list)

    # Summary over the stable region only.
    median_f0_hz: float = 0.0
    cents_std: float = 0.0          # pitch stability; high => not a sustained note
    overall_confidence: Confidence = Confidence.NOT_ENOUGH_AUDIO


@dataclass
class HarmonicModel:
    """Per-harmonic amplitude, measured — not assumed.

    Amplitudes are stored in dB relative to the loudest harmonic so the model is
    level-independent: the same voice recorded quieter must produce the same
    instrument.
    """

    f0_hz: float = 0.0
    harmonic_index: list[int] = field(default_factory=list)
    frequency_hz: list[float] = field(default_factory=list)   # measured, not k*f0
    amplitude_db: list[float] = field(default_factory=list)   # relative to max
    phase_rad: list[float] = field(default_factory=list)      # at the anchor frame
    confidence: list[float] = field(default_factory=list)
    n_harmonics: int = 0
    # Above this, harmonics were below the noise floor and are NOT trustworthy.
    reliable_up_to_hz: float = 0.0


@dataclass
class SpectralEnvelope:
    """The vocal tract, in absolute Hz.

    ABSOLUTE FREQUENCY IS THE ENTIRE POINT. Storing the envelope relative to f0
    would make it move with pitch, which is what a sampler does and what this
    instrument exists not to do.
    """

    method: str = ""                                  # lpc | cepstral | true | peak
    frequency_hz: list[float] = field(default_factory=list)
    magnitude_db: list[float] = field(default_factory=list)
    confidence: Confidence = Confidence.NOT_ENOUGH_AUDIO
    notes: str = ""


@dataclass
class Formant:
    center_hz: float = 0.0
    bandwidth_hz: float = 0.0
    gain_db: float = 0.0
    confidence: Confidence = Confidence.LOW


@dataclass
class FormantModel:
    """Optional decoration on top of the envelope.

    The envelope is the artifact; formant labels are a convenience. Nasal vowels
    have anti-formants that no peak list represents, so an empty list here with a
    good envelope is a SUCCESS, not a failure.
    """

    formants: list[Formant] = field(default_factory=list)
    confidence: Confidence = Confidence.LOW


@dataclass
class NoiseModel:
    """Breath and aspiration, as a spectrum to re-generate — never a loop."""

    overall_level_db: float = -120.0
    frequency_hz: list[float] = field(default_factory=list)
    magnitude_db: list[float] = field(default_factory=list)
    harmonic_to_noise_db: float = 0.0
    confidence: Confidence = Confidence.NOT_ENOUGH_AUDIO


@dataclass
class AmplitudeEnvelope:
    """Measured curve first; ADSR derived from it for the UI.

    Forcing every vocal input into a textbook ADSR loses exactly the shapes that
    make one singer's note sound different from another's.
    """

    times_s: list[float] = field(default_factory=list)
    level: list[float] = field(default_factory=list)      # 0..1 normalised
    attack_s: float = 0.0
    decay_s: float = 0.0
    sustain_level: float = 1.0
    release_s: float = 0.0
    confidence: Confidence = Confidence.NOT_ENOUGH_AUDIO


@dataclass
class LFOTarget:
    destination: str = ""       # amplitude | pitch | envelope | tilt | noise
    depth: float = 0.0
    unit: str = ""              # cents | db | ratio


@dataclass
class DetectedLFO:
    """A measured modulation, stored as its ACTUAL folded shape.

    The shape table is the point. Replacing a real vibrato with a sine loses the
    asymmetry that makes it sound like a person; the template label is for the UI
    only and never drives synthesis.
    """

    rate_hz: float = 0.0
    shape_table: list[float] = field(default_factory=list)   # 128 points, folded
    shape_label: str = ""                                    # UI only
    phase_at_onset: float = 0.0
    confidence: Confidence = Confidence.NOT_ENOUGH_AUDIO
    targets: list[LFOTarget] = field(default_factory=list)
    cycles_observed: float = 0.0    # < ~2.5 => cannot separate from envelope


@dataclass
class VoiceInstrumentModel:
    version: int = SCHEMA_VERSION
    source: SourceMetadata = field(default_factory=SourceMetadata)
    stable_region: StableRegion = field(default_factory=StableRegion)
    pitch: PitchTrack = field(default_factory=PitchTrack)
    harmonics: HarmonicModel = field(default_factory=HarmonicModel)
    envelope: SpectralEnvelope = field(default_factory=SpectralEnvelope)
    formants: FormantModel = field(default_factory=FormantModel)
    noise: NoiseModel = field(default_factory=NoiseModel)
    amplitude: AmplitudeEnvelope = field(default_factory=AmplitudeEnvelope)
    lfos: list[DetectedLFO] = field(default_factory=list)

    analysis_confidence: Confidence = Confidence.NOT_ENOUGH_AUDIO
    warnings: list[str] = field(default_factory=list)

    # --- serialisation ---------------------------------------------------
    def to_json(self, indent: int = 2) -> str:
        return json.dumps(asdict(self), indent=indent, default=str)

    def save(self, path: str) -> None:
        with open(path, "w") as handle:
            handle.write(self.to_json())

    @staticmethod
    def load(path: str) -> dict[str, Any]:
        """Returns the raw dict, not a model.

        Loading is deliberately not automatic: an older schema needs a migration
        decision, and silently constructing a current-version object from old
        fields is how a model quietly means something different than it did.
        """
        with open(path) as handle:
            data = json.load(handle)
        if data.get("version") != SCHEMA_VERSION:
            raise ValueError(
                f"model schema v{data.get('version')} != v{SCHEMA_VERSION}; migrate explicitly"
            )
        return data

    def warn(self, message: str) -> None:
        if message not in self.warnings:
            self.warnings.append(message)
