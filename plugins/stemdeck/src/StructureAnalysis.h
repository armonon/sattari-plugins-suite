#pragma once

// =============================================================================
//  StructureAnalysis — segment a track into intro / build / drop / breakdown /
//  outro from its per-bar energy. Feeds the arrangement view's structure ribbon
//  (brief: "structure markers turn track knowledge from memory into something
//  visible"). Heuristic, energy-based; a learned segmenter is a later upgrade.
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

namespace stemdeck
{

struct Section
{
    double startBeat = 0.0, endBeat = 0.0;
    juce::String label;      // INTRO / BUILD / DROP / BREAKDOWN / OUTRO
    float energy = 0.0f;     // 0..1, normalised
};

inline std::vector<Section> segmentStructure (const juce::AudioBuffer<float>& audio, double sr,
                                              double bpm, int beatsPerBar = 4)
{
    std::vector<Section> out;
    const int n = audio.getNumSamples();
    if (n <= 0 || sr <= 0.0 || bpm <= 1.0) return out;

    const double barSamples = (double) beatsPerBar * 60.0 / bpm * sr;
    if (barSamples < 1.0) return out;
    const int bars = (int) std::floor (n / barSamples);
    if (bars < 2) return out;

    // Per-bar RMS across channels.
    std::vector<float> e ((size_t) bars, 0.0f);
    const int ch = juce::jmax (1, audio.getNumChannels());
    for (int b = 0; b < bars; ++b)
    {
        const int s0 = (int) (b * barSamples), s1 = (int) ((b + 1) * barSamples);
        double sum = 0.0; int cnt = 0;
        for (int c = 0; c < ch; ++c) { const auto* d = audio.getReadPointer (c); for (int i = s0; i < s1 && i < n; ++i) { sum += (double) d[i] * d[i]; ++cnt; } }
        e[(size_t) b] = cnt ? (float) std::sqrt (sum / cnt) : 0.0f;
    }
    float mx = 1.0e-9f; for (float v : e) mx = juce::jmax (mx, v);
    for (float& v : e) v /= mx;   // normalise to peak

    // Classify each bar, then merge runs. Labels are contextual: low energy before
    // any drop = INTRO, after = BREAKDOWN; the final low run = OUTRO.
    auto classify = [] (float x) { return x >= 0.66f ? 2 : x <= 0.34f ? 0 : 1; };   // 0 low, 1 mid, 2 high
    bool seenDrop = false;
    int runStart = 0, runClass = classify (e[0]);
    auto emit = [&] (int b0, int b1, int cls, float avg)
    {
        juce::String lbl = cls == 2 ? "DROP" : cls == 1 ? "BUILD" : (seenDrop ? "BREAKDOWN" : "INTRO");
        if (cls == 2) seenDrop = true;
        Section s; s.startBeat = b0 * (double) beatsPerBar; s.endBeat = b1 * (double) beatsPerBar; s.label = lbl; s.energy = avg;
        out.push_back (s);
    };
    for (int b = 1; b <= bars; ++b)
    {
        const int cls = b < bars ? classify (e[(size_t) b]) : -1;
        if (cls != runClass)
        {
            float avg = 0.0f; for (int k = runStart; k < b; ++k) avg += e[(size_t) k]; avg /= juce::jmax (1, b - runStart);
            emit (runStart, b, runClass, avg);
            runStart = b; runClass = cls;
        }
    }
    // Rename a trailing low/mid run to OUTRO once a drop has happened.
    if (! out.empty() && seenDrop && out.back().label != "DROP")
        out.back().label = "OUTRO";
    return out;
}

} // namespace stemdeck
