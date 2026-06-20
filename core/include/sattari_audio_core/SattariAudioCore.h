#pragma once

#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sattari::audio_core {

enum class ReleaseGate {
    Prototype,
    InternalAlpha,
    ValidatedAlpha,
    Beta,
    PublicBeta,
    ReleaseCandidate,
    SaleReady
};

inline const char* toString(ReleaseGate gate) noexcept {
    switch (gate) {
        case ReleaseGate::Prototype: return "prototype";
        case ReleaseGate::InternalAlpha: return "internal alpha";
        case ReleaseGate::ValidatedAlpha: return "validated alpha";
        case ReleaseGate::Beta: return "beta";
        case ReleaseGate::PublicBeta: return "public beta";
        case ReleaseGate::ReleaseCandidate: return "release candidate";
        case ReleaseGate::SaleReady: return "sale-ready";
    }
    return "unknown";
}

struct AudioRenderContext {
    double sampleRate = 48000.0;
    std::uint32_t blockSize = 512;
    std::uint32_t channelCount = 2;
    bool offline = false;
};

struct ExportRequest {
    std::string productId;
    std::string format;       // wav, midi, png, mp4, json, preset-bank
    std::string destination;
    std::map<std::string, std::string> metadata;
};

struct RenderManifest {
    std::string productId;
    std::string productVersion;
    std::string sourceCommit;
    std::string presetId;
    std::string seed;
    std::vector<std::string> inputFiles;
    std::vector<std::string> outputFiles;
    std::map<std::string, std::string> metrics;
    ReleaseGate truthGate = ReleaseGate::Prototype;
};

struct PresetDescriptor {
    std::string id;
    std::string displayName;
    std::string category;
    std::string author = "Sattari Audio";
    std::string schemaVersion = "1";
    std::map<std::string, double> numericParameters;
    std::map<std::string, std::string> textParameters;
};

struct MidiNoteEvent {
    int note = 60;
    int velocity = 96;
    double beat = 0.0;
    double durationBeats = 0.25;
    int channel = 10;
};

struct MidiPatternDescriptor {
    std::string id;
    std::string genre;
    std::string section;
    double bpm = 120.0;
    double swing = 0.0;
    double humanize = 0.0;
    std::string kitMap = "general-midi-drums";
    std::vector<MidiNoteEvent> notes;
};

struct ListeningTestItem {
    std::string clipId;
    std::string listener;
    std::string environment;
    int musicalUsefulness1to5 = 0;
    int artifactSeverity1to5 = 0;
    int confidence1to5 = 0;
    bool pass = false;
    std::string notes;
};

struct ValidationMetric {
    std::string name;
    double value = 0.0;
    std::optional<double> passThreshold;
    bool pass = false;
    std::string notes;
};

struct ProductValidationReport {
    std::string productId;
    std::string productVersion;
    ReleaseGate currentGate = ReleaseGate::Prototype;
    ReleaseGate targetGate = ReleaseGate::InternalAlpha;
    std::vector<ValidationMetric> metrics;
    std::vector<ListeningTestItem> listeningTests;
    std::vector<std::string> knownLimitations;
    std::vector<std::string> blockers;
};

struct PluginShellConfig {
    std::string productId;
    std::string displayName;
    std::string manufacturer = "Sattari Audio";
    bool buildAU = true;
    bool buildVST3 = true;
    bool buildStandalone = true;
    bool needsMidiInput = false;
    bool producesMidiOutput = false;
    bool isSynth = false;
};

enum class MusicalMode {
    Major,
    Minor,
    Unknown
};

inline const char* toString(MusicalMode mode) noexcept {
    switch (mode) {
        case MusicalMode::Major: return "major";
        case MusicalMode::Minor: return "minor";
        case MusicalMode::Unknown: return "unknown";
    }
    return "unknown";
}

struct KeySignature {
    // 0=C, 1=C#/Db, ... 11=B. Use -1 when the key is unknown.
    int tonicPitchClass = -1;
    MusicalMode mode = MusicalMode::Unknown;
    double confidence = 0.0;
};

struct HarmonicWheelCode {
    // Sattari's own wheel code: number 1-12 around the circle of fifths,
    // lane M for major and m for minor. This intentionally avoids copying
    // any competitor naming/branding while preserving the musical workflow.
    int number = 0;
    char lane = '?';
};

enum class HarmonicMove {
    Unknown,
    PerfectLock,
    SmoothSideStep,
    RelativeMode,
    EnergyLift,
    EnergyDrop,
    RiskyClash
};

inline const char* toString(HarmonicMove move) noexcept {
    switch (move) {
        case HarmonicMove::Unknown: return "unknown";
        case HarmonicMove::PerfectLock: return "perfect lock";
        case HarmonicMove::SmoothSideStep: return "smooth side-step";
        case HarmonicMove::RelativeMode: return "relative mode";
        case HarmonicMove::EnergyLift: return "energy lift";
        case HarmonicMove::EnergyDrop: return "energy drop";
        case HarmonicMove::RiskyClash: return "risky clash";
    }
    return "unknown";
}

inline int positiveModulo(int value, int modulus) noexcept {
    const auto remainder = value % modulus;
    return remainder < 0 ? remainder + modulus : remainder;
}

inline HarmonicWheelCode toSattariWheelCode(KeySignature key) noexcept {
    if (key.tonicPitchClass < 0 || key.mode == MusicalMode::Unknown) {
        return {};
    }

    // Circle-of-fifths order, indexed to keep C major / A minor at 8.
    // Pitch classes: B, F#, Db, Ab, Eb, Bb, F, C, G, D, A, E.
    static constexpr int majorWheelByPitchClass[12] = {
        8,  // C
        3,  // C#/Db
        10, // D
        5,  // D#/Eb
        12, // E
        7,  // F
        2,  // F#/Gb
        9,  // G
        4,  // G#/Ab
        11, // A
        6,  // A#/Bb
        1   // B
    };

    const auto tonic = positiveModulo(key.tonicPitchClass, 12);
    if (key.mode == MusicalMode::Major) {
        return {majorWheelByPitchClass[tonic], 'M'};
    }

    // Relative minor shares the same wheel number as its relative major.
    // Example: A minor -> C major -> 8m.
    const auto relativeMajor = positiveModulo(tonic + 3, 12);
    return {majorWheelByPitchClass[relativeMajor], 'm'};
}

inline std::string toDisplayString(HarmonicWheelCode code) {
    if (code.number <= 0 || code.lane == '?') {
        return "unknown";
    }
    return std::to_string(code.number) + code.lane;
}

inline HarmonicMove classifyHarmonicMove(KeySignature from, KeySignature to) noexcept {
    const auto a = toSattariWheelCode(from);
    const auto b = toSattariWheelCode(to);
    if (a.number <= 0 || b.number <= 0) {
        return HarmonicMove::Unknown;
    }

    const auto distance = positiveModulo(b.number - a.number, 12);
    const auto sameLane = a.lane == b.lane;
    if (a.number == b.number && sameLane) {
        return HarmonicMove::PerfectLock;
    }
    if (a.number == b.number && !sameLane) {
        return HarmonicMove::RelativeMode;
    }
    if ((distance == 1 || distance == 11) && sameLane) {
        return HarmonicMove::SmoothSideStep;
    }
    if (distance == 1 && !sameLane) {
        return HarmonicMove::EnergyLift;
    }
    if (distance == 11 && !sameLane) {
        return HarmonicMove::EnergyDrop;
    }
    return HarmonicMove::RiskyClash;
}

struct EnergySegment {
    double startSeconds = 0.0;
    double durationSeconds = 0.0;
    double energy0to1 = 0.0;
    std::string label;
};

enum class CuePointKind {
    Intro,
    Verse,
    Hook,
    Chorus,
    Drop,
    Breakdown,
    Outro,
    Custom
};

inline const char* toString(CuePointKind kind) noexcept {
    switch (kind) {
        case CuePointKind::Intro: return "intro";
        case CuePointKind::Verse: return "verse";
        case CuePointKind::Hook: return "hook";
        case CuePointKind::Chorus: return "chorus";
        case CuePointKind::Drop: return "drop";
        case CuePointKind::Breakdown: return "breakdown";
        case CuePointKind::Outro: return "outro";
        case CuePointKind::Custom: return "custom";
    }
    return "custom";
}

struct CuePoint {
    double seconds = 0.0;
    CuePointKind kind = CuePointKind::Custom;
    std::string label;
    bool snappedToPlayback = false;
};

struct TrackAnalysis {
    std::string trackId;
    std::string title;
    std::string artist;
    double bpm = 0.0;
    KeySignature key;
    double energy0to1 = 0.0;
    std::vector<EnergySegment> energySegments;
    std::vector<CuePoint> cuePoints;
    std::map<std::string, std::string> tags;
};

inline int energyLevel1to10(double energy0to1) noexcept {
    if (energy0to1 <= 0.0) return 1;
    if (energy0to1 >= 1.0) return 10;
    return 1 + static_cast<int>(energy0to1 * 9.999);
}

} // namespace sattari::audio_core
