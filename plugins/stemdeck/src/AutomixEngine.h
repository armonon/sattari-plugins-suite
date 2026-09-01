#pragma once

// Automix planner: given two analysed tracks (outgoing A, incoming B), decide
// HOW and WHERE to transition like a competent DJ would — tempo-match B to A,
// align both tracks to musical phrase boundaries, place the mix in A's outro and
// B's first main section, script a bass-swap crossfade, and check harmonic
// (Camelot) compatibility. This sits above BeatSyncEngine (which does the
// per-deck beat-matching); this decides the transition itself.
//
// Pure logic over TrackAnalysis (+ optional per-phrase energy) so it is unit
// testable without the audio engine.

#include <JuceHeader.h>
#include <algorithm>
#include <vector>
#include "TrackAnalysis.h"

namespace stemdeck
{

enum class TransitionRecipe { smooth = 0, bassSwap, drumBridge, vocalHandoff };

inline juce::String transitionRecipeName (TransitionRecipe recipe)
{
    switch (recipe)
    {
        case TransitionRecipe::smooth:       return "Smooth Blend";
        case TransitionRecipe::bassSwap:     return "Bass Swap";
        case TransitionRecipe::drumBridge:   return "Drum Bridge";
        case TransitionRecipe::vocalHandoff: return "Vocal Handoff";
    }
    return "Smooth Blend";
}

struct TransitionPlan
{
    bool   valid = false;
    double outPointSampleA = 0.0;      // where the blend begins on deck A
    double inPointSampleB  = 0.0;      // where deck B is cued to enter
    int    lengthBeats     = 32;       // transition length in beats
    double tempoRatioBtoA  = 1.0;      // time-stretch applied to B to match A
    double bassSwapFraction = 0.5;     // 0..1 through the blend where B's bass takes over
    bool   keyCompatible   = false;    // A and B are harmonically compatible as-is
    int    suggestedSemitoneShiftB = 0;// pitch shift on B that makes it compatible (0 if already)
    TransitionRecipe recipe = TransitionRecipe::smooth;
    bool stemMorphEnabled = true;
    juce::String description;
};

class AutomixEngine
{
public:
    struct Options
    {
        int  phraseBeats     = 16;     // phrase length (16 beats = 4 bars in 4/4)
        int  transitionBeats = 32;     // desired blend length
        bool harmonicMatch   = true;   // consider Camelot compatibility / suggest shift
        TransitionRecipe recipe = TransitionRecipe::smooth;
        bool stemMorph = true;
    };

    // ── Camelot harmonic mixing ───────────────────────────────────────────────
    // Camelot number for a key (major = "B" ring, minor = "A" ring). 1..12.
    static int camelotNumber (int tonicPc, bool minor) noexcept
    {
        const int pc = ((minor ? tonicPc + 3 : tonicPc) % 12 + 12) % 12;   // minor -> relative major
        const int k  = (pc * 7) % 12;                                      // steps of a fifth from C
        return ((7 + k) % 12) + 1;
    }

    // Standard harmonic-mixing adjacency: same key, relative major/minor (same
    // number, other ring), or ±1 on the wheel (same ring).
    static bool keysCompatible (const TrackAnalysis& a, const TrackAnalysis& b) noexcept
    {
        if (! a.hasKey() || ! b.hasKey())
            return false;
        const int na = camelotNumber (a.keyPitchClass, a.keyIsMinor);
        const int nb = camelotNumber (b.keyPitchClass, b.keyIsMinor);
        const int diff = wheelDistance (na, nb);
        if (a.keyIsMinor == b.keyIsMinor)
            return diff <= 1;                 // same ring: 0 (same key) or ±1
        return diff == 0;                     // different ring: only relative maj/min
    }

    // Phrase boundaries (sample positions) anchored to the detected downbeat and
    // stepped every phraseBeats beats along the actual tracked beat grid.
    static std::vector<double> phraseBoundaries (const TrackAnalysis& t, int phraseBeats)
    {
        std::vector<double> out;
        const auto& g = t.beatGridSamples;
        if (g.size() < 2 || phraseBeats < 1)
            return out;

        // Index of the beat closest to the detected downbeat.
        int db = 0; double bestD = 1.0e18;
        for (int i = 0; i < (int) g.size(); ++i)
        {
            const double d = std::abs (g[(size_t) i] - t.downbeatSample);
            if (d < bestD) { bestD = d; db = i; }
        }
        for (int i = db % phraseBeats; i < (int) g.size(); i += phraseBeats)
            out.push_back (g[(size_t) i]);
        return out;
    }

    // Per-phrase RMS energy (parallel to the boundaries; last phrase runs to EOF).
    static std::vector<float> phraseEnergies (const juce::AudioBuffer<float>& audio, double /*sr*/,
                                              const std::vector<double>& boundaries)
    {
        std::vector<float> e;
        if (boundaries.empty() || audio.getNumSamples() <= 0)
            return e;
        const int n = audio.getNumSamples();
        const int ch = audio.getNumChannels();
        for (size_t p = 0; p < boundaries.size(); ++p)
        {
            const int s0 = juce::jlimit (0, n, (int) boundaries[p]);
            const int s1 = juce::jlimit (0, n, p + 1 < boundaries.size() ? (int) boundaries[p + 1] : n);
            double sum = 0.0; int cnt = 0;
            for (int c = 0; c < ch; ++c)
            {
                const auto* d = audio.getReadPointer (c);
                for (int i = s0; i < s1; ++i) { sum += (double) d[i] * d[i]; ++cnt; }
            }
            e.push_back (cnt > 0 ? (float) std::sqrt (sum / (double) cnt) : 0.0f);
        }
        return e;
    }

    static TransitionPlan planTransition (const TrackAnalysis& a, const TrackAnalysis& b,
                                          const std::vector<float>& energyA,
                                          const std::vector<float>& energyB)
    {
        return planTransition (a, b, energyA, energyB, Options{});
    }

    static TransitionPlan planTransition (const TrackAnalysis& a, const TrackAnalysis& b,
                                          const std::vector<float>& energyA,
                                          const std::vector<float>& energyB,
                                          const Options& opt)
    {
        TransitionPlan plan;
        const auto phrasesA = phraseBoundaries (a, opt.phraseBeats);
        const auto phrasesB = phraseBoundaries (b, opt.phraseBeats);
        if (phrasesA.size() < 2 || phrasesB.size() < 2)
        {
            plan.description = "Not enough phrase structure to auto-mix.";
            return plan;
        }

        // Tempo match: stretch B onto A, folding to the nearest half/double so we
        // never stretch beyond ~±6% for tracks an octave apart in tempo.
        plan.tempoRatioBtoA = tempoMatchRatio (a.detectedBpm, b.detectedBpm);

        // Out-point on A: a phrase boundary late in the track (last third) with
        // the lowest energy — the outro. Without energy, take ~85% through.
        plan.outPointSampleA = chooseOutPhrase (phrasesA, energyA);

        // In-point on B: the first phrase after the intro whose energy reaches the
        // track's median — the main section / first drop. Without energy, phrase 1.
        plan.inPointSampleB = chooseInPhrase (phrasesB, energyB);

        plan.lengthBeats = opt.transitionBeats;
        plan.bassSwapFraction = 0.5;   // swap lows at the blend midpoint (classic move)
        plan.recipe = opt.recipe;
        plan.stemMorphEnabled = opt.stemMorph;

        if (opt.harmonicMatch)
        {
            plan.keyCompatible = keysCompatible (a, b);
            if (! plan.keyCompatible)
                plan.suggestedSemitoneShiftB = suggestCompatibleShift (a, b);
        }

        plan.valid = true;
        plan.description = describe (a, b, plan);
        return plan;
    }

private:
    static int wheelDistance (int na, int nb) noexcept
    {
        int d = std::abs (na - nb) % 12;
        return std::min (d, 12 - d);
    }

    static double tempoMatchRatio (float bpmA, float bpmB) noexcept
    {
        if (bpmA <= 0.0f || bpmB <= 0.0f) return 1.0;
        double r = (double) bpmA / (double) bpmB;
        while (r > 1.41) r *= 0.5;      // fold octaves so we stretch the short way
        while (r < 0.71) r *= 2.0;
        return r;
    }

    static double chooseOutPhrase (const std::vector<double>& phrases, const std::vector<float>& energy)
    {
        const int n = (int) phrases.size();
        const int startIdx = juce::jmax (1, (n * 2) / 3);      // last third
        if (energy.size() == phrases.size())
        {
            int best = startIdx; float bestE = 1.0e9f;
            for (int i = startIdx; i < n - 1; ++i)             // leave room for the blend
                if (energy[(size_t) i] < bestE) { bestE = energy[(size_t) i]; best = i; }
            return phrases[(size_t) best];
        }
        return phrases[(size_t) juce::jlimit (0, n - 1, (int) std::round (n * 0.85))];
    }

    static double chooseInPhrase (const std::vector<double>& phrases, const std::vector<float>& energy)
    {
        const int n = (int) phrases.size();
        if (energy.size() == phrases.size())
        {
            std::vector<float> sorted (energy);
            std::sort (sorted.begin(), sorted.end());
            const float median = sorted[sorted.size() / 2];
            for (int i = 1; i < n; ++i)                        // skip phrase 0 (intro)
                if (energy[(size_t) i] >= median) return phrases[(size_t) i];
        }
        return phrases[(size_t) juce::jmin (1, n - 1)];
    }

    // Smallest |shift| in [-3,+3] semitones that makes B compatible with A.
    static int suggestCompatibleShift (const TrackAnalysis& a, const TrackAnalysis& b) noexcept
    {
        for (int mag = 1; mag <= 3; ++mag)
            for (int s : { mag, -mag })
            {
                TrackAnalysis shifted = b;
                shifted.keyPitchClass = ((b.keyPitchClass + s) % 12 + 12) % 12;
                if (keysCompatible (a, shifted)) return s;
            }
        return 0;
    }

    static juce::String describe (const TrackAnalysis& a, const TrackAnalysis& b, const TransitionPlan& p)
    {
        juce::String s;
        s << transitionRecipeName (p.recipe) << " over " << p.lengthBeats << " beats. ";
        const double pct = (double) (int) (p.tempoRatioBtoA * 1000.0) / 10.0;
        s << "Match B to A (x" << juce::String (p.tempoRatioBtoA, 3) << ", " << juce::String (pct, 1) << "%). ";
        if (a.hasKey() && b.hasKey())
        {
            s << a.keyName() << " -> " << b.keyName() << " ";
            if (p.keyCompatible)                 s << "(harmonic).";
            else if (p.suggestedSemitoneShiftB)  s << "(clash - shift B " << (p.suggestedSemitoneShiftB > 0 ? "+" : "")
                                                   << p.suggestedSemitoneShiftB << " st).";
            else                                 s << "(key clash).";
        }
        return s;
    }
};

} // namespace stemdeck
