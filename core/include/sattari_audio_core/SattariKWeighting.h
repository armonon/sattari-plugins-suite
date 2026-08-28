#pragma once

#include <algorithm>
#include <cmath>

// =============================================================================
//  ITU-R BS.1770 K-weighting + true-peak helpers.
// -----------------------------------------------------------------------------
//  Loudness measurement needs three small, exactly-specified pieces: a direct-form
//  biquad, the two K-weighting stages (a +4 dB high shelf at 1681.97 Hz and a
//  38.135 Hz high pass, both from the standard), and inter-sample peak detection.
//
//  Meter and Peak each had their own copy of all three — Peak's own comments said
//  "identical form to Sattari Meter" and "Matches Sattari Meter", which is a
//  duplication admitting itself. Two copies of a standards-defined filter is two
//  chances to mistype a coefficient, so they live here once and are contract-tested.
//
//  Pure-std (no JUCE), so the contract can cover it directly.
// =============================================================================

namespace sattari::dsp
{

// Direct-form-I biquad. Coefficients are pre-normalised by a0.
struct KBiquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void reset() noexcept { x1 = x2 = y1 = y2 = 0; }

    double process (double x) noexcept
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// BS.1770 stage 1: high shelf, fc 1681.97 Hz, +3.9997 dB, S = 1.
inline void designKWeightingShelf (KBiquad& b, double sr) noexcept
{
    constexpr double pi = 3.14159265358979323846;
    const double A  = std::pow (10.0, 3.9997 / 40.0);
    const double w0 = 2.0 * pi * 1681.974450 / sr;
    const double c = std::cos (w0), s = std::sin (w0);
    const double alpha = s * 0.5 * std::sqrt (2.0);
    const double tsa = 2.0 * std::sqrt (A) * alpha;
    const double a0 = (A + 1.0) - (A - 1.0) * c + tsa;

    b.b0 =  A * ((A + 1.0) + (A - 1.0) * c + tsa) / a0;
    b.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0;
    b.b2 =  A * ((A + 1.0) + (A - 1.0) * c - tsa) / a0;
    b.a1 =  2.0 * ((A - 1.0) - (A + 1.0) * c) / a0;
    b.a2 =  ((A + 1.0) - (A - 1.0) * c - tsa) / a0;
    b.reset();
}

// BS.1770 stage 2: high pass, fc 38.135 Hz, Q 0.5.
inline void designKWeightingHighPass (KBiquad& b, double sr) noexcept
{
    constexpr double pi = 3.14159265358979323846;
    const double w0 = 2.0 * pi * 38.135470 / sr;
    const double c = std::cos (w0), s = std::sin (w0);
    const double alpha = s / (2.0 * 0.5);
    const double a0 = 1.0 + alpha;

    b.b0 = ((1.0 + c) * 0.5) / a0;
    b.b1 = (-(1.0 + c)) / a0;
    b.b2 = ((1.0 + c) * 0.5) / a0;
    b.a1 = (-2.0 * c) / a0;
    b.a2 = (1.0 - alpha) / a0;
    b.reset();
}

// Inter-sample (true) peak across one sample step: 4x linear interpolation between
// the previous sample and the current one.
inline float interSamplePeak (float prev, float cur) noexcept
{
    float m = std::fabs (cur);
    for (int k = 1; k < 4; ++k)
    {
        const float v = prev + (cur - prev) * (float) k / 4.0f;
        m = std::max (m, std::fabs (v));
    }
    return m;
}

} // namespace sattari::dsp
