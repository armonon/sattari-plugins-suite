#pragma once

// StemGain — the canonical stem-gain unit for StemDeck.
//
// Stem gain is stored and transported as a LINEAR PERCENT in [0, 200]:
//
//     0   = silence            (x0.0)
//     100 = unity, at rest     (x1.0,  0 dB)
//     200 = maximum boost      (x2.0, +6 dB)
//
// Percent (not dB) is the stored unit on purpose: automation around unity is then
// symmetric and linear (100 sits exactly in the middle of the knob's +/-135 sweep),
// and the ">100% boost" concept survives a round-trip through the event stream
// without being clamped at unity.
//
// ONE-WAY DOOR — read before changing:
// Before schema v2 the stem gain was stored in dB over [-60, +12] with 0.0 meaning
// UNITY. Under the percent scheme 0.0 means SILENCE. The two number spaces overlap
// but mean opposite things, so any persisted value MUST be migrated, never
// reinterpreted. Migration helpers live below; the call sites are the APVTS state
// (MainPluginProcessor::setStateInformation) and the session JSON
// (SessionModel::fromVar). Both key off an explicit stored version.

#include <juce_audio_basics/juce_audio_basics.h>

namespace stemdeck::stemgain
{

inline constexpr float kMin   = 0.0f;     // silence
inline constexpr float kUnity = 100.0f;   // x1.0 (0 dB) — the knob's rest position
inline constexpr float kMax   = 200.0f;   // x2.0 (+6 dB)

// Legacy (schema v1) dB range, kept only so migration is exact.
inline constexpr float kLegacyMinDb = -60.0f;
inline constexpr float kLegacyMaxDb =  12.0f;

/** Percent [0,200] -> linear gain multiplier [0,2]. This is what the DSP consumes. */
inline float percentToLinear (float percent) noexcept
{
    return juce::jlimit (kMin, kMax, percent) * 0.01f;
}

/** Linear gain multiplier -> percent, clamped to the representable range. */
inline float linearToPercent (float linear) noexcept
{
    return juce::jlimit (kMin, kMax, linear * 100.0f);
}

/** Percent -> dB, for display only (-inf at silence). */
inline float percentToDb (float percent) noexcept
{
    return juce::Decibels::gainToDecibels (percentToLinear (percent), -100.0f);
}

/** Migrate one schema-v1 dB value to a v2 percent value.
    0 dB -> 100 (unity), -60 dB -> ~0 (silence), +12 dB -> clamped to 200 (+6 dB).
    Note the boost ceiling drops from +12 dB to +6 dB; values above +6 dB clamp,
    which is deliberate — the knob's documented top is x2.0. */
inline float legacyDbToPercent (float db) noexcept
{
    const float clamped = juce::jlimit (kLegacyMinDb, kLegacyMaxDb, db);
    return juce::jlimit (kMin, kMax, juce::Decibels::decibelsToGain (clamped) * 100.0f);
}

/** Display string for the knob readout: "100", "0" or "MUTE". */
inline juce::String displayText (float percent, bool muted)
{
    if (muted) return "MUTE";
    return juce::String (juce::roundToInt (juce::jlimit (kMin, kMax, percent)));
}

} // namespace stemdeck::stemgain
