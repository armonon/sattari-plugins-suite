#pragma once

#include <JuceHeader.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>
#include "AutomixEngine.h"

namespace stemdeck::performance
{
// A single source of truth for the musical curves shared by live automation,
// the Flow panel, and tests. These helpers allocate nothing and are safe to use
// while the audio callback is building the current transition frame.
struct StemMorphFrame
{
    std::array<float, 4> outgoing { 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, 4> incoming { 0.0f, 0.0f, 0.0f, 0.0f };
    float outgoingFullMix = 1.0f;
    float incomingFullMix = 0.0f;
};

class StemMorphEngine
{
public:
    static StemMorphFrame frame (TransitionRecipe recipe, float progress) noexcept
    {
        StemMorphFrame result;
        const float t = juce::jlimit (0.0f, 1.0f, progress);
        const auto swaps = swapPoints (recipe);
        for (size_t stem = 0; stem < swaps.size(); ++stem)
        {
            // Each handoff occupies 22% of the complete transition. A constant-sum
            // curve preserves energy while still producing an obvious musical swap.
            const float x = smoothstep ((t - (swaps[stem] - 0.11f)) / 0.22f);
            result.outgoing[stem] = 1.0f - x;
            result.incoming[stem] = x;
        }
        const float full = smoothstep (t);
        result.outgoingFullMix = 1.0f - full;
        result.incomingFullMix = full;
        return result;
    }

    static std::array<float, 4> swapPoints (TransitionRecipe recipe) noexcept
    {
        // Order: drums, bass, music/other, vocals.
        switch (recipe)
        {
            case TransitionRecipe::smooth:       return { 0.50f, 0.50f, 0.50f, 0.50f };
            case TransitionRecipe::bassSwap:     return { 0.68f, 0.50f, 0.38f, 0.30f };
            case TransitionRecipe::drumBridge:   return { 0.78f, 0.46f, 0.34f, 0.25f };
            case TransitionRecipe::vocalHandoff: return { 0.32f, 0.43f, 0.55f, 0.74f };
        }
        return { 0.50f, 0.50f, 0.50f, 0.50f };
    }

private:
    static float smoothstep (float x) noexcept
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    }
};

enum class MacroProfile { build = 0, echoOut, spaceBloom, filterLift };

struct AdaptiveMacroFrame
{
    float reverb = 0.0f;
    float echo = 0.0f;
    float filter = 0.0f;     // bipolar deck filter, -1..1
    float stemSend = 0.0f;
};

class AdaptiveFxMacro
{
public:
    static AdaptiveMacroFrame frame (MacroProfile profile, float value) noexcept
    {
        const float x = juce::jlimit (0.0f, 1.0f, value);
        const float eased = x * x * (3.0f - 2.0f * x);
        switch (profile)
        {
            case MacroProfile::build:
                return { 0.42f * eased, 0.58f * eased,
                         0.72f * eased, 0.50f * eased };
            case MacroProfile::echoOut:
                return { 0.12f * eased, 0.92f * eased,
                         -0.82f * eased, 0.28f * eased };
            case MacroProfile::spaceBloom:
                return { 0.90f * eased, 0.26f * eased,
                         0.18f * eased, 0.86f * eased };
            case MacroProfile::filterLift:
                return { 0.20f * eased, 0.34f * eased,
                         0.94f * eased, 0.36f * eased };
        }
        return {};
    }
};

struct SetCandidate
{
    int index = -1;
    float bpm = 0.0f;
    int keyPitchClass = -1;
    bool minor = false;
    float energy = 0.5f;
};

class SmartSetAssistant
{
public:
    static float compatibilityScore (const SetCandidate& current,
                                     const SetCandidate& candidate) noexcept
    {
        if (candidate.index < 0) return -1000.0f;
        const float tempoDistance = normalizedTempoDistance (current.bpm, candidate.bpm);
        float score = 100.0f - tempoDistance * 8.0f;
        if (harmonic (current, candidate)) score += 28.0f;
        else if (current.keyPitchClass >= 0 && candidate.keyPitchClass >= 0) score -= 14.0f;
        // Slightly favour a controlled energy rise; never overpower a clean mix.
        score += juce::jlimit (-8.0f, 8.0f,
                               (candidate.energy - current.energy) * 16.0f);
        return score;
    }

    static std::vector<int> rank (const SetCandidate& current,
                                  const std::vector<SetCandidate>& candidates)
    {
        std::vector<std::pair<float, int>> scored;
        scored.reserve (candidates.size());
        for (const auto& candidate : candidates)
            if (candidate.index != current.index)
                scored.emplace_back (compatibilityScore (current, candidate), candidate.index);
        std::stable_sort (scored.begin(), scored.end(), [] (const auto& a, const auto& b)
        {
            return a.first > b.first;
        });
        std::vector<int> result;
        result.reserve (scored.size());
        for (const auto& item : scored) result.push_back (item.second);
        return result;
    }

private:
    static float normalizedTempoDistance (float a, float b) noexcept
    {
        if (a <= 1.0f || b <= 1.0f) return 12.0f;
        float best = std::abs (a - b);
        best = juce::jmin (best, std::abs (a - b * 0.5f));
        best = juce::jmin (best, std::abs (a - b * 2.0f));
        return best;
    }

    static bool harmonic (const SetCandidate& a, const SetCandidate& b) noexcept
    {
        if (a.keyPitchClass < 0 || b.keyPitchClass < 0) return false;
        TrackAnalysis ta, tb;
        ta.keyPitchClass = a.keyPitchClass; ta.keyIsMinor = a.minor; ta.keyConfidence = 1.0f;
        tb.keyPitchClass = b.keyPitchClass; tb.keyIsMinor = b.minor; tb.keyConfidence = 1.0f;
        return AutomixEngine::keysCompatible (ta, tb);
    }
};

enum class FindingSeverity { info, warning, critical };

struct RehearsalFinding
{
    FindingSeverity severity = FindingSeverity::info;
    juce::String title;
    juce::String action;
};

struct RehearsalInput
{
    int loadedDecks = 0;
    int analyzedDecks = 0;
    int storedScenes = 0;
    bool hasRecordingDestination = true;
    bool audioCallbackHealthy = true;
    bool missingMedia = false;
    bool looperArmedWithoutSignal = false;
    int pluginFaults = 0;
    int droppedBuffers = 0;
    float cpuLoad = 0.0f;
    float masterPeak = 0.0f;
};

struct RehearsalReport
{
    int score = 100;
    std::vector<RehearsalFinding> findings;
    bool showReady() const noexcept
    {
        return std::none_of (findings.begin(), findings.end(), [] (const auto& f)
        { return f.severity == FindingSeverity::critical; });
    }
};

class RehearsalCoach
{
public:
    static RehearsalReport assess (const RehearsalInput& in)
    {
        RehearsalReport report;
        auto add = [&report] (FindingSeverity severity, int penalty,
                              juce::String title, juce::String action)
        {
            report.score = juce::jmax (0, report.score - penalty);
            report.findings.push_back ({ severity, std::move (title), std::move (action) });
        };
        if (! in.audioCallbackHealthy)
            add (FindingSeverity::critical, 45, "Audio output is offline",
                 "Open Audio/MIDI Setup and choose an active output device.");
        if (in.missingMedia)
            add (FindingSeverity::critical, 35, "Project media is missing",
                 "Run Repair Missing Files before the show.");
        if (in.pluginFaults > 0)
            add (FindingSeverity::warning, 15, juce::String (in.pluginFaults) + " plugin helper fault(s)",
                 "The affected insert is dry-bypassed; rescan or replace it before stage time.");
        if (in.droppedBuffers > 0)
            add (FindingSeverity::critical, 30, "Audio deadline misses detected",
                 "Increase the buffer or enable Performance Safe Mode.");
        if (in.cpuLoad > 0.85f)
            add (FindingSeverity::warning, 18, "CPU headroom is low",
                 "Freeze heavy processing or increase the audio buffer.");
        if (in.masterPeak > 0.995f)
            add (FindingSeverity::warning, 10, "Master is repeatedly touching the ceiling",
                 "Lower the master or stem buses and inspect limiter gain reduction.");
        if (in.loadedDecks < 2)
            add (FindingSeverity::info, 5, "No transition pair is prepared",
                 "Load or queue a second track.");
        if (in.loadedDecks > 0 && in.analyzedDecks < in.loadedDecks)
            add (FindingSeverity::warning, 10, "Some tracks lack complete grid/key analysis",
                 "Let Sets finish analysis or correct the beat grid manually.");
        if (in.storedScenes == 0)
            add (FindingSeverity::info, 4, "No recovery scene is stored",
                 "Shift-click S1 to capture a known-good mix state.");
        if (in.looperArmedWithoutSignal)
            add (FindingSeverity::warning, 6, "Looper is armed while the mix is silent",
                 "Start the source or cancel the pending capture.");
        if (! in.hasRecordingDestination)
            add (FindingSeverity::warning, 8, "Recording destination is not confirmed",
                 "Start a short test capture before performing.");
        if (report.findings.empty())
            report.findings.push_back ({ FindingSeverity::info, "Show path looks ready",
                                         "Run one full transition and a short recording check." });
        return report;
    }
};

struct ContinuityInput
{
    bool callbackHealthy = true;
    float cpuLoad = 0.0f;
    int droppedBuffers = 0;
    int pluginFaults = 0;
};

class EmergencyContinuity
{
public:
    struct Decision
    {
        bool engageSafeMode = false;
        bool pluginDryFallback = false;
        juce::String reason;
    };

    Decision update (const ContinuityInput& in, bool automaticEnabled)
    {
        Decision result;
        const int newDrops = juce::jmax (0, in.droppedBuffers - previousDrops);
        previousDrops = in.droppedBuffers;
        overloadStreak = in.cpuLoad > 0.94f ? overloadStreak + 1 : 0;
        offlineStreak = ! in.callbackHealthy ? offlineStreak + 1 : 0;
        if (in.callbackHealthy) offlineStreak = 0;

        if (in.pluginFaults > 0)
        {
            result.pluginDryFallback = true;
            result.reason = "A crashed plugin helper was replaced by its dry path";
        }
        if (! automaticEnabled) return result;
        if (newDrops >= 2 || overloadStreak >= 3)
        {
            result.engageSafeMode = true;
            result.reason = "Performance Safe Mode engaged after repeated audio overloads";
        }
        else if (offlineStreak >= 2)
        {
            result.engageSafeMode = true;
            result.reason = "Output disappeared; the returning device will resume on the dry safe path";
        }
        return result;
    }

    void reset() noexcept { previousDrops = overloadStreak = offlineStreak = 0; }

private:
    int previousDrops = 0;
    int overloadStreak = 0;
    int offlineStreak = 0;
};
} // namespace stemdeck::performance
