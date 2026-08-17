#pragma once

// One gain language throughout StemDeck's UI:
//
//     left edge  = minimum/cut
//     centre     = 100% (unity, 0 dB)
//     right edge = 300% (x3.0, +9.54 dB)
//
// Most older parameters are still stored in dB so existing projects and host
// automation keep their meaning. These helpers only define the presentation and
// gesture mapping. Stem gains are already stored directly as linear percent.

#include <juce_audio_basics/juce_audio_basics.h>

namespace stemdeck::gainscale
{

inline constexpr float kMinDb      = -60.0f;
inline constexpr float kMaxLinear  = 3.0f;
inline constexpr float kUnity      = 1.0f;
inline constexpr float kMaxPercent = 300.0f;
inline constexpr float kMaxDb      = 9.542425f; // 20 * log10 (3)

inline float linearToControlNorm (float linear) noexcept
{
    const float g = juce::jlimit (0.0f, kMaxLinear, linear);
    return g <= kUnity ? g * 0.5f
                       : 0.5f + (g - kUnity) * (0.5f / (kMaxLinear - kUnity));
}

inline float controlNormToLinear (float norm) noexcept
{
    const float n = juce::jlimit (0.0f, 1.0f, norm);
    return n <= 0.5f ? n * 2.0f
                     : kUnity + (n - 0.5f) * 2.0f * (kMaxLinear - kUnity);
}

inline float dbToPercent (float db) noexcept
{
    return db <= kMinDb ? 0.0f
                        : juce::jlimit (0.0f, kMaxPercent,
                                        juce::Decibels::decibelsToGain (db) * 100.0f);
}

inline float percentToDb (float percent) noexcept
{
    const float linear = juce::jlimit (0.0f, kMaxPercent, percent) * 0.01f;
    return juce::Decibels::gainToDecibels (linear, kMinDb);
}

// A centre-detented dB control. This is preferable to a linear dB mapping: unity
// must be physically centred even when the cut and boost ranges are asymmetric.
inline float bipolarDbToNorm (float db, float minimumDb = kMinDb,
                              float maximumDb = kMaxDb) noexcept
{
    const float value = juce::jlimit (minimumDb, maximumDb, db);
    if (value <= 0.0f)
        return 0.5f * (value - minimumDb) / (0.0f - minimumDb);
    return 0.5f + 0.5f * value / maximumDb;
}

inline float normToBipolarDb (float norm, float minimumDb = kMinDb,
                              float maximumDb = kMaxDb) noexcept
{
    const float n = juce::jlimit (0.0f, 1.0f, norm);
    return n <= 0.5f ? minimumDb + (n * 2.0f) * (0.0f - minimumDb)
                     : (n - 0.5f) * 2.0f * maximumDb;
}

inline juce::String percentTextFromDb (float db)
{
    return juce::String (juce::roundToInt (dbToPercent (db)));
}

} // namespace stemdeck::gainscale
