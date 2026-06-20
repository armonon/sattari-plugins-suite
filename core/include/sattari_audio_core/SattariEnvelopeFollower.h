#pragma once

// =============================================================================
//  Sattari Audio Suite — shared asymmetric envelope follower
// -----------------------------------------------------------------------------
//  The attack/release one-pole follower that Bus, Side Chain, Sub Conjurer, and
//  Royal Chain each hand-rolled identically:
//
//      coeff = (x > env) ? attack : release;        // exp(-1 / (sr * timeSec))
//      env   = x + coeff * (env - x);
//
//  Pure-std (no JUCE) so it is unit-testable and cheap to include anywhere.
//  Bit-for-bit equivalent to the inlined copies it replaces.
// =============================================================================

#include <cmath>

namespace sattari::dsp
{

class EnvelopeFollower
{
public:
    void prepare (double sampleRate, float attackMs = 10.0f, float releaseMs = 100.0f)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        setTimes (attackMs, releaseMs);
        reset();
    }

    // Recompute coefficients (call per block when attack/release are parameters).
    void setTimes (float attackMs, float releaseMs) noexcept
    {
        attackCoeff  = coeffFor (attackMs);
        releaseCoeff = coeffFor (releaseMs);
    }

    void reset (float value = 0.0f) noexcept { env = value; }
    float current() const noexcept { return env; }

    // Track an already-leveled detector value (e.g. a max of |L|,|R|).
    float process (float x) noexcept
    {
        const float c = x > env ? attackCoeff : releaseCoeff;
        env = x + c * (env - x);
        return env;
    }

    // Convenience for the common rectified peak follower.
    float processRectified (float x) noexcept { return process (std::fabs (x)); }

    // Same primitive, inverted polarity: "fast attack" when the target moves DOWN
    // (used for gain-reduction smoothing, where more-negative dB is the attack
    // direction). Mathematically equivalent to negating both x and env.
    float processFalling (float x) noexcept
    {
        const float c = x < env ? attackCoeff : releaseCoeff;
        env = x + c * (env - x);
        return env;
    }

private:
    float coeffFor (float ms) const noexcept
    {
        const float t = ms * 0.001f;
        return t > 0.0f ? std::exp (-1.0f / (float) (sr * (double) t)) : 0.0f;
    }

    double sr = 44100.0;
    float env = 0.0f, attackCoeff = 0.0f, releaseCoeff = 0.0f;
};

} // namespace sattari::dsp
