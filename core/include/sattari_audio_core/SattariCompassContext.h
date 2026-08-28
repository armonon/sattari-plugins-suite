#pragma once

// =============================================================================
//  Sattari Compass — semantic session-context helpers
// -----------------------------------------------------------------------------
//  The session bus carries neutral facts: key, swing, section, energy, and so on.
//  Musical products should not each invent a different interpretation of those
//  facts. These small pure helpers are the first shared response contract.
//
//  Every response is explicitly opt-in and falls back to the local control when
//  Compass is absent. That keeps old sessions sonically identical and prevents a
//  detector from making surprising musical changes by merely being instantiated.
// =============================================================================

#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <cmath>

namespace sattari::compass
{

inline juce::String sectionName (int sectionId)
{
    static const char* names[] = {
        "None", "Intro", "Verse", "Pre-chorus", "Chorus", "Bridge", "Outro"
    };
    return names[juce::jlimit (0, 6, sectionId)];
}

inline float effectiveSwing (
    float localSwing,
    bool followCompass,
    const xplugin::SessionSnapshot& session) noexcept
{
    if (! followCompass || ! session.valid || ! session.hasSceneContext)
        return juce::jlimit (0.0f, 0.65f, localSwing);

    // The session vocabulary is normalized 0..1. Arp's musical swing range is
    // deliberately capped at 0.65 so the off-beat never crosses the next step.
    return juce::jlimit (0.0f, 0.65f, session.swing * 0.65f);
}

inline int effectiveOctaves (
    int localOctaves,
    bool followCompass,
    const xplugin::SessionSnapshot& session) noexcept
{
    const int local = juce::jlimit (1, 4, localOctaves);
    if (! followCompass || ! session.valid || ! session.hasSceneContext)
        return local;

    // High-energy sections gain one octave of range. Lower-energy sections keep
    // the authored pattern rather than silently removing notes.
    return juce::jlimit (1, 4, local + (session.energy >= 0.75f ? 1 : 0));
}

inline int effectiveVelocity (
    int localVelocity,
    bool followCompass,
    const xplugin::SessionSnapshot& session) noexcept
{
    const int local = juce::jlimit (1, 127, localVelocity);
    if (! followCompass || ! session.valid || ! session.hasSceneContext)
        return local;

    const float energy = juce::jlimit (0.0f, 1.0f, session.energy);
    const float contour = 0.58f + 0.42f * energy;
    const int lift = energy >= 0.75f ? 8 : 0;
    return juce::jlimit (1, 127, (int) std::lround ((float) local * contour) + lift);
}

} // namespace sattari::compass
