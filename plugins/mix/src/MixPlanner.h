#pragma once

#include "MixAnalysis.h"
#include <vector>

namespace sattari::automix
{
enum class Role { instrument, lead, backing, kick, bass, drums, effects };
inline juce::StringArray roleNames() { return { "Instrument", "Lead vocal", "Backing vocal", "Kick", "Bass", "Drums", "Effects" }; }

struct Settings
{
    float gain = 0, low = 0, presence = 0, compression = 0, threshold = -18;
    float amount = 1, trim = 0;
    bool enabled = false;
};

inline bool equivalent (const Settings& a, const Settings& b)
{
    return std::abs (a.gain - b.gain) < 0.02f && std::abs (a.low - b.low) < 0.02f
        && std::abs (a.presence - b.presence) < 0.02f && std::abs (a.compression - b.compression) < 0.02f
        && std::abs (a.threshold - b.threshold) < 0.02f && std::abs (a.amount - b.amount) < 0.002f
        && std::abs (a.trim - b.trim) < 0.02f && a.enabled == b.enabled;
}

struct Track
{
    juce::String id, name, capture, ack, error;
    Role role = Role::instrument;
    bool locked = false;
    Measurement measured;
    Settings settings;
};

struct Suggestion
{
    Track source;
    Settings proposed;
    juce::String reason;
};

struct Plan
{
    std::vector<Suggestion> tracks;
    juce::String error;
    float estimatedPeakDb = -100;
    bool valid() const { return error.isEmpty() && ! tracks.empty(); }
};

inline float effectiveGain (const Settings& s) { return (s.enabled ? s.gain * s.amount : 0.0f) + s.trim; }
inline double amplitude (float db) { return std::pow (10.0, (double) db / 20.0); }
inline float decibels (double gain) { return gain > 1.0e-10 ? (float) (20.0 * std::log10 (gain)) : -100.0f; }

inline Plan makePlan (const std::vector<Track>& tracks, const juce::String& capture)
{
    Plan plan;
    if (tracks.size() < 2) { plan.error = "At least two linked tracks are required."; return plan; }
    if (tracks.size() > 64) { plan.error = "A mix group supports up to 64 tracks."; return plan; }
    int audible = 0, leads = 0;
    for (const auto& t : tracks)
    {
        if (capture.isEmpty() || t.capture != capture || ! t.measured.complete)
        { plan.error = "Finish the same analysis pass on every linked track."; return plan; }
        if (t.measured.invalid) { plan.error = "Invalid audio or changed processing on " + t.name + ". Analyze again."; return plan; }
        if (t.measured.activeSeconds > 0 && t.measured.activeSeconds < 1.0f)
        { plan.error = "Too little active audio on " + t.name + ". Analyze a longer passage."; return plan; }
        if (t.measured.activeSeconds >= 1.0f)
        {
            ++audible;
            if (t.role == Role::lead) ++leads;
        }
    }
    if (audible < 2) { plan.error = "At least two tracks need one second of active audio."; return plan; }
    if (leads > 1) { plan.error = "Choose one lead vocal; set doubles to Backing vocal."; return plan; }

    bool hasKick = false, hasBass = false;
    for (const auto& t : tracks)
        if (t.measured.activeSeconds >= 1.0f)
        { hasKick = hasKick || t.role == Role::kick; hasBass = hasBass || t.role == Role::bass; }

    for (const auto& t : tracks)
    {
        Suggestion s { t, t.settings, {} };
        if (t.locked) s.reason = "Locked";
        else if (t.measured.activeSeconds < 1.0f) s.reason = "Silent passage; unchanged";
        else
        {
            static constexpr float target[] = { -28, -20, -28, -24, -25, -25, -32 };
            const auto role = (size_t) t.role;
            s.proposed = {};
            s.proposed.enabled = true;
            s.proposed.gain = juce::jlimit (-36.0f, 6.0f, target[role] - t.measured.loudness);
            s.proposed.gain = juce::jmin (s.proposed.gain, -9.0f - t.measured.peakDb);
            if (s.proposed.gain < -36.0f)
            { plan.error = "Source peaks exceed the gain range on " + t.name + ". Lower the input and analyze again."; return plan; }
            const bool support = t.role == Role::instrument || t.role == Role::backing || t.role == Role::effects;
            if (support && leads > 0 && t.measured.presence > 0.06f)
                s.proposed.presence = -juce::jlimit (0.5f, 2.5f, t.measured.presence * 5.0f);
            if (support && (hasKick || hasBass) && t.measured.low > 0.12f)
                s.proposed.low = -juce::jlimit (0.5f, 3.0f, t.measured.low * 4.0f);
            if ((t.role == Role::lead || t.role == Role::backing || t.role == Role::bass)
                && t.measured.crest > 12.0f)
            {
                s.proposed.compression = juce::jlimit (0.0f, 1.0f, (t.measured.crest - 12.0f) / 12.0f);
                s.proposed.threshold = juce::jlimit (-48.0f, -6.0f, t.measured.peakDb - 8.0f);
            }
            s.reason = roleNames()[(int) t.role] + " balance";
            if (s.proposed.low < 0) s.reason += "; low-end space";
            if (s.proposed.presence < 0) s.reason += "; vocal space";
            if (s.proposed.compression > 0) s.reason += "; gentle dynamics";
        }
        plan.tracks.push_back (s);
    }

    // Worst-case coincident sample peaks. This is deliberately a bound, not a
    // true-peak prediction or a claim about unknown downstream DAW routing.
    double fixed = 0, adjustable = 0;
    for (const auto& s : plan.tracks)
    {
        const double p = amplitude (s.source.measured.peakDb + effectiveGain (s.proposed));
        if (s.source.locked || s.source.measured.activeSeconds < 1) fixed += p;
        else adjustable += p;
    }
    const double ceiling = amplitude (-6.0f);
    if (fixed >= ceiling)
    { plan.error = "Locked tracks leave insufficient estimated headroom. Lower them or unlock them."; return plan; }
    if (fixed + adjustable > ceiling)
    {
        const float reduction = decibels ((ceiling - fixed) / adjustable);
        for (auto& s : plan.tracks)
            if (! s.source.locked && s.source.measured.activeSeconds >= 1)
            {
                s.proposed.gain += reduction;
                if (s.proposed.gain < -36)
                { plan.error = "Required attenuation exceeds 36 dB. Lower source levels and analyze again."; return plan; }
            }
    }
    double peak = 0;
    for (const auto& s : plan.tracks) peak += amplitude (s.source.measured.peakDb + effectiveGain (s.proposed));
    plan.estimatedPeakDb = decibels (peak);
    return plan;
}

inline float comparisonTrim (const Plan& p, bool proposed)
{
    double before = 0, after = 0;
    for (const auto& s : p.tracks)
    {
        if (s.source.measured.activeSeconds < 1) continue;
        before += std::pow (10.0, (s.source.measured.loudness + effectiveGain (s.source.settings)) / 10.0);
        after += std::pow (10.0, (s.source.measured.loudness + effectiveGain (s.proposed)) / 10.0);
    }
    const double reference = std::min (before, after);
    const double current = proposed ? after : before;
    return current > 1.0e-12 ? juce::jlimit (-36.0f, 0.0f, (float) (10.0 * std::log10 (reference / current))) : 0.0f;
}
} // namespace sattari::automix
