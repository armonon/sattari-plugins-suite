#pragma once

// =============================================================================
//  Sattari Vocal Master — pure DSP helpers (no JUCE)
// -----------------------------------------------------------------------------
//  The maths the vocal-cleanup chain leans on, factored out so it unit-tests on
//  its own like the rest of the shared core: stereo "isolate" (mid-side music
//  reduction — the realtime stand-in for AI stem removal), a downward-expander
//  gain for the de-noise gate, and the tone-tilt shelf mapping.
// =============================================================================

#include <algorithm>
#include <cmath>

namespace sattari::vocal
{

// Mid-side "isolate": vocals usually sit in the centre, music in the sides, so
// scaling the side signal down pushes the music back while keeping the vocal.
// isolate 0 = untouched stereo, 1 = mono centre (sides fully removed).
inline void isolateCentre (float l, float r, float isolate, float& outL, float& outR) noexcept
{
    const float mid  = 0.5f * (l + r);
    const float side = 0.5f * (l - r) * (1.0f - std::min (1.0f, std::max (0.0f, isolate)));
    outL = mid + side;
    outR = mid - side;
}

// Downward-expander gain for the de-noise gate: full gain above the threshold,
// gently falling to `floorGain` below it (so quiet noise/bleed is pushed down
// without hard chattering). env and thr are linear magnitudes.
inline float expanderGain (float env, float thr, float floorGain) noexcept
{
    if (thr <= 0.0f) return 1.0f;
    if (env >= thr)  return 1.0f;
    const float t = env / thr;                 // 0..1 below the threshold
    return floorGain + (1.0f - floorGain) * t; // linear fade to the floor
}

// Map a single Tone control (-1 dark .. +1 bright) to complementary low/high
// shelf gains in dB (a tilt EQ).
inline void tiltGainsDb (float tone, float& lowDb, float& highDb) noexcept
{
    tone = std::min (1.0f, std::max (-1.0f, tone));
    constexpr float range = 6.0f;   // ±6 dB tilt
    highDb =  tone * range;
    lowDb  = -tone * range;
}

} // namespace sattari::vocal
