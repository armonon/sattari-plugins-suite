#pragma once

// Fractional-sample readers for the deck's random-access playback (resampling,
// granular time-stretch, scratch). The default was a 2-point linear read, which
// rolls off highs and adds imaging distortion whenever the file's sample rate
// differs from the output rate. This provides a 16-tap Blackman-windowed sinc
// read: near-transparent SR conversion, and — because sinc(0) is a unit delta —
// still bit-exact when the read lands exactly on a sample (matched-rate,
// no-stretch playback stays a pass-through).

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace stemdeck
{

// 2-point linear interpolation (the previous behaviour; kept for A/B measurement).
inline float readLinear (const juce::AudioBuffer<float>& audio, int channel, double position) noexcept
{
    const int numSamples = audio.getNumSamples();
    const int numChannels = audio.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0 || !std::isfinite (position))
        return 0.0f;

    const int c  = juce::jlimit (0, numChannels - 1, channel);
    position = juce::jlimit (0.0, (double) (numSamples - 1), position);
    const int i0 = juce::jlimit (0, numSamples - 1, (int) position);
    const int i1 = juce::jmin (i0 + 1, numSamples - 1);
    const float frac = (float) (position - (double) i0);
    const auto* data = audio.getReadPointer (c);
    return data[i0] + frac * (data[i1] - data[i0]);
}

// Windowed-sinc kernel table. 16 taps (half-width 8) × 512 fractional phases,
// each phase normalised to unit sum so DC / steady tones keep their level.
// Built once, at static-init time — never allocates on the audio thread.
class SincKernelTable
{
public:
    static constexpr int halfWidth = 8;                 // taps span i0-7 .. i0+8
    static constexpr int numTaps   = 2 * halfWidth;     // 16
    static constexpr int numPhases = 512;

    SincKernelTable() noexcept
    {
        for (int p = 0; p < numPhases; ++p)
        {
            const double frac = (double) p / (double) numPhases;   // [0, 1)
            double* row = weights[p];
            double sum = 0.0;

            for (int t = 0; t < numTaps; ++t)
            {
                const int    j = t - (halfWidth - 1);              // -7 .. +8
                const double x = frac - (double) j;                // sinc argument
                const double w = sinc (x) * blackman (x);
                row[t] = w;
                sum += w;
            }

            const double inv = (std::abs (sum) > 1.0e-12) ? 1.0 / sum : 1.0;
            for (int t = 0; t < numTaps; ++t)
                row[t] *= inv;                                     // unity DC gain
        }
    }

    // channel/index-clamped windowed-sinc read at a fractional sample position.
    float read (const juce::AudioBuffer<float>& audio, int channel, double position) const noexcept
    {
        const int numSamples = audio.getNumSamples();
        const int numChannels = audio.getNumChannels();
        if (numSamples <= 0 || numChannels <= 0 || !std::isfinite (position))
            return 0.0f;

        const int    c    = juce::jlimit (0, numChannels - 1, channel);
        if (numSamples == 1)
            return audio.getSample (c, 0);
        // Clamp before floor/cast: invalid or overshooting transport positions
        // must not extrapolate, overflow an integer, or contaminate audio.
        position = juce::jlimit (0.0, (double) (numSamples - 1), position);
        const int    i0   = (int) std::floor (position);
        const double frac = position - (double) i0;
        int ph = (int) (frac * (double) numPhases);
        ph = juce::jlimit (0, numPhases - 1, ph);
        const double* row = weights[ph];
        const auto* data  = audio.getReadPointer (c);

        double acc = 0.0;
        for (int t = 0; t < numTaps; ++t)
        {
            const int idx = juce::jlimit (0, numSamples - 1, i0 - (halfWidth - 1) + t);
            acc += (double) data[idx] * row[t];
        }
        return (float) acc;
    }

private:
    static double sinc (double x) noexcept
    {
        if (std::abs (x) < 1.0e-9)
            return 1.0;
        const double px = juce::MathConstants<double>::pi * x;
        return std::sin (px) / px;
    }

    // Blackman window over the kernel support [-halfWidth, halfWidth]; 0 outside.
    static double blackman (double x) noexcept
    {
        if (std::abs (x) > (double) halfWidth)
            return 0.0;
        const double a = juce::MathConstants<double>::pi * x / (double) halfWidth;
        return 0.42 + 0.5 * std::cos (a) + 0.08 * std::cos (2.0 * a);
    }

    double weights[numPhases][numTaps];
};

inline const SincKernelTable& sincTable() noexcept
{
    static const SincKernelTable table;   // thread-safe one-time init (C++11)
    return table;
}

inline float readSinc (const juce::AudioBuffer<float>& audio, int channel, double position) noexcept
{
    return sincTable().read (audio, channel, position);
}

} // namespace stemdeck
