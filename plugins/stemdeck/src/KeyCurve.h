#pragma once

// KeyCurve — key-over-time for dynamic key lock (perform-screen §6.2, Phase 0).
//
// StemDeck's neural key detection (NeuralKeyEngine) emits ONE key per track. Tracks
// modulate (8A intro -> 9A final chorus), so to hold a track in key through its own
// modulations we first need key AS A FUNCTION OF TIME. This header is the pure,
// headlessly-testable core: window the track, take per-window key observations from a
// caller-supplied detector, then median-smooth to reject single-window flips and mark
// where the track actually modulates.
//
// Deliberately free of the neural model + audio buffers so it unit-tests without the
// ONNX runtime; production passes NeuralKeyEngine::detect as the window detector.

#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>

namespace stemdeck
{

struct KeyObservation
{
    double atBeat     = 0.0;
    int    pitchClass = 0;      // 0..11
    bool   minor      = false;
    float  confidence = 0.0f;   // 0..1
};

// Analysis-window centres (start beats) across a track. winBeats is the detection
// window length, hopBeats the stride (e.g. 16-bar window, 4-bar hop at 4/4).
inline std::vector<double> keyWindowStarts (double totalBeats, double winBeats, double hopBeats)
{
    std::vector<double> out;
    if (totalBeats <= 0.0 || winBeats <= 0.0 || hopBeats <= 0.0) return out;
    for (double b = 0.0; b + winBeats <= totalBeats + 1e-6; b += hopBeats)
        out.push_back (b);
    if (out.empty()) out.push_back (0.0);
    return out;
}

// Median-smooth a raw per-window curve so one bad window can't fake a modulation.
// The median is taken over pitch class treated cyclically around the local mode.
inline std::vector<KeyObservation> smoothKeyCurve (std::vector<KeyObservation> raw, int medianWin = 3)
{
    const int n = (int) raw.size();
    if (n <= 2 || medianWin < 3) return raw;
    const int half = medianWin / 2;
    std::vector<KeyObservation> out = raw;
    for (int i = 0; i < n; ++i)
    {
        // Mode of pitchClass+minor over the window is a robust "typical key".
        std::array<int, 24> votes {};   // 12 pitch classes x 2 rings
        for (int j = juce::jmax (0, i - half); j <= juce::jmin (n - 1, i + half); ++j)
            votes[(size_t) (raw[(size_t) j].pitchClass + (raw[(size_t) j].minor ? 12 : 0))]++;
        int best = 0, bestSlot = raw[(size_t) i].pitchClass + (raw[(size_t) i].minor ? 12 : 0);
        for (int s = 0; s < 24; ++s) if (votes[(size_t) s] > best) { best = votes[(size_t) s]; bestSlot = s; }
        out[(size_t) i].pitchClass = bestSlot % 12;
        out[(size_t) i].minor      = bestSlot >= 12;
    }
    return out;
}

// A contiguous key segment (used for the modulation visualisation + the "modulates to
// X in the chorus" prompt).
struct KeySegment { double startBeat = 0.0, endBeat = 0.0; int pitchClass = 0; bool minor = false; };

inline std::vector<KeySegment> segmentKeyCurve (const std::vector<KeyObservation>& curve, double endBeat)
{
    std::vector<KeySegment> segs;
    for (const auto& o : curve)
    {
        if (! segs.empty() && segs.back().pitchClass == o.pitchClass && segs.back().minor == o.minor)
            continue;   // same key continues
        if (! segs.empty()) segs.back().endBeat = o.atBeat;
        segs.push_back ({ o.atBeat, endBeat, o.pitchClass, o.minor });
    }
    if (! segs.empty()) segs.back().endBeat = endBeat;
    return segs;
}

// True if the track modulates (more than one distinct key segment).
inline bool keyCurveModulates (const std::vector<KeyObservation>& curve)
{
    for (size_t i = 1; i < curve.size(); ++i)
        if (curve[i].pitchClass != curve[0].pitchClass || curve[i].minor != curve[0].minor)
            return true;
    return false;
}

} // namespace stemdeck
