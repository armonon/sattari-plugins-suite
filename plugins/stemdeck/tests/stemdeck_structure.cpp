// StructureAnalysis check: a quiet -> loud -> quiet signal must yield a DROP
// section in the middle, with earlier/later sections around it, covering the
// track. Heuristic segmenter, so this locks the coarse behaviour, not exact bars.
#include "StructureAnalysis.h"
#include <iostream>
#include <string>
#include <cmath>

using namespace stemdeck;

namespace { int fails = 0; void check (bool ok, const std::string& w) { std::cout << "  " << (ok?"OK  ":"FAIL") << "  " << w << "\n"; if(!ok) ++fails; } }

int main()
{
    const double SR = 44100.0, BPM = 120.0;
    const double barSec = 4.0 * 60.0 / BPM;      // 2 s/bar
    const int bars = 16, n = (int) (SR * barSec * bars);
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const double frac = (double) i / n;
        // quiet intro (0-0.25), loud middle (0.35-0.7 = the drop), quiet outro.
        const double amp = (frac > 0.35 && frac < 0.70) ? 0.8 : 0.12;
        const float v = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / SR));
        b.setSample (0, i, v); b.setSample (1, i, v);
    }

    const auto secs = segmentStructure (b, SR, BPM);
    check (secs.size() >= 2, "produces multiple sections");

    // There is a DROP, and it sits in the middle of the track.
    bool dropInMiddle = false;
    double trackBeats = 0; for (auto& s : secs) trackBeats = juce::jmax (trackBeats, s.endBeat);
    for (const auto& s : secs)
        if (s.label == "DROP")
        {
            const double mid = (s.startBeat + s.endBeat) / 2.0 / juce::jmax (1.0, trackBeats);
            if (mid > 0.3 && mid < 0.75) dropInMiddle = true;
        }
    check (dropInMiddle, "a DROP section is detected in the middle (the loud region)");

    // Sections are contiguous and cover the track from 0.
    bool contiguous = ! secs.empty() && secs.front().startBeat == 0.0;
    for (size_t i = 1; i < secs.size(); ++i) if (std::abs (secs[i].startBeat - secs[i-1].endBeat) > 1e-6) contiguous = false;
    check (contiguous, "sections are contiguous from beat 0");

    // The DROP is the highest-energy section.
    float dropE = -1, maxE = 0;
    for (const auto& s : secs) { maxE = juce::jmax (maxE, s.energy); if (s.label == "DROP") dropE = juce::jmax (dropE, s.energy); }
    check (dropE >= maxE - 1e-6f, "the DROP is the highest-energy section");

    std::cout << (fails == 0 ? "STRUCTURE_OK" : "STRUCTURE_FAIL (" + std::to_string (fails) + ")") << "\n";
    return fails == 0 ? 0 : 1;
}
