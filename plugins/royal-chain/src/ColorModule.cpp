#include "ColorModule.h"
#include <cmath>

void ColorModule::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    // Pre-size ADAA state so process() never allocates on the audio thread.
    x1.assign (8, 0.0f);
    reset();
}

void ColorModule::reset()
{
    std::fill (x1.begin(), x1.end(), 0.0f);
}

namespace
{
// Numerically-stable ln(cosh(z)) = |z| - ln2 + log1p(exp(-2|z|)).
inline double logcosh (double z) noexcept
{
    const double a = std::abs (z);
    return a - 0.6931471805599453 + std::log1p (std::exp (-2.0 * a));
}
} // namespace

void ColorModule::process (juce::AudioBuffer<float>& buffer, float glow, float royal)
{
    const float drive = 1.0f + 1.75f * juce::jlimit (0.0f, 1.0f, glow) + 0.65f * royal;
    const float mix = juce::jlimit (0.0f, 0.65f, glow * 0.42f + royal * 0.12f);
    const float compensate = 1.0f / (1.0f + 0.25f * (drive - 1.0f));

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) x1.size());

    // ADAA on g(x) = tanh(drive * x). Its antiderivative is
    //   G(x) = (1/drive) * ln(cosh(drive * x)),
    // and the 1st-order ADAA output is (G(x) - G(xPrev)) / (x - xPrev). When two
    // consecutive samples are nearly equal the quotient is ill-conditioned, so we
    // fall back to the direct nonlinearity at their midpoint.
    const double d = (double) drive;
    const double invD = 1.0 / d;
    constexpr double eps = 1.0e-5;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        double xPrev = (double) x1[(size_t) ch];
        double gPrev = invD * logcosh (d * xPrev);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double x = (double) data[i];
            const double g = invD * logcosh (d * x);
            double sat;
            if (std::abs (x - xPrev) > eps)
                sat = (g - gPrev) / (x - xPrev);
            else
                sat = std::tanh (d * 0.5 * (x + xPrev));   // midpoint fallback

            const float wet = (float) sat * compensate;
            data[i] = (float) (x + mix * (wet - x));
            xPrev = x; gPrev = g;
        }
        x1[(size_t) ch] = (float) xPrev;
    }
}
