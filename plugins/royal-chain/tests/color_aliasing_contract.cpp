// Royal Chain Color — anti-aliasing contract.
//
// Proves the ADAA tanh saturator aliases LESS than a naive per-sample tanh.
// Drives a high 7 kHz sine hard through both; tanh generates harmonics above
// Nyquist that fold back down as energy BELOW the fundamental. We FFT each
// output and sum magnitude in [300 Hz, 6 kHz] (a band that should be ~empty
// without aliasing) and assert the ADAA path has materially less of it.
#include "ColorModule.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <cstdio>

namespace
{
// Naive per-sample reference matching the PRE-ADAA ColorModule maths.
void naiveColor (juce::AudioBuffer<float>& buf, float glow, float royal)
{
    const float drive = 1.0f + 1.75f * juce::jlimit (0.0f, 1.0f, glow) + 0.65f * royal;
    const float mix = juce::jlimit (0.0f, 0.65f, glow * 0.42f + royal * 0.12f);
    const float compensate = 1.0f / (1.0f + 0.25f * (drive - 1.0f));
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        auto* d = buf.getWritePointer (ch);
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const float dry = d[i];
            const float wet = std::tanh (dry * drive) * compensate;
            d[i] = dry + mix * (wet - dry);
        }
    }
}

// Sum spectral magnitude in [loHz, hiHz] of a mono buffer.
double bandEnergy (const juce::AudioBuffer<float>& buf, double sr, double loHz, double hiHz)
{
    const int order = 14;                 // 16384-point FFT
    const int n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> fb ((size_t) (n * 2), 0.0f);
    const int copy = juce::jmin (n, buf.getNumSamples());
    for (int i = 0; i < copy; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (copy - 1));
        fb[(size_t) i] = buf.getSample (0, i) * w;
    }
    fft.performFrequencyOnlyForwardTransform (fb.data());
    const double binHz = sr / n;
    const int lo = juce::jmax (1, (int) std::ceil (loHz / binHz));
    const int hi = juce::jmin (n / 2 - 1, (int) std::floor (hiHz / binHz));
    double e = 0.0;
    for (int k = lo; k <= hi; ++k) e += (double) fb[(size_t) k];
    return e;
}

void fillSine (juce::AudioBuffer<float>& buf, double sr, double hz, float gain)
{
    double ph = 0.0;
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        const float s = gain * (float) std::sin (ph);
        ph += juce::MathConstants<double>::twoPi * hz / sr;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch) buf.setSample (ch, i, s);
    }
}
} // namespace

int main()
{
    const double sr = 48000.0;
    const int n = 1 << 14;
    const double f0 = 7000.0;          // high enough that harmonics fold below it
    const float glow = 1.0f, royal = 1.0f;   // heavy drive

    juce::AudioBuffer<float> naive (1, n), adaa (1, n);
    fillSine (naive, sr, f0, 0.9f);
    fillSine (adaa,  sr, f0, 0.9f);

    naiveColor (naive, glow, royal);

    ColorModule color;
    color.prepare (sr);
    color.process (adaa, glow, royal);

    // Aliasing lands as energy in a band well below the fundamental.
    const double naiveAlias = bandEnergy (naive, sr, 300.0, 6000.0);
    const double adaaAlias  = bandEnergy (adaa,  sr, 300.0, 6000.0);
    const double reductionDb = 20.0 * std::log10 ((adaaAlias + 1e-12) / (naiveAlias + 1e-12));

    std::printf ("alias band energy: naive=%.4f  ADAA=%.4f  reduction=%.1f dB\n",
                 naiveAlias, adaaAlias, reductionDb);

    if (adaaAlias >= naiveAlias * 0.85)
    {
        std::fprintf (stderr, "FAIL: ADAA did not reduce aliasing vs naive tanh (%.1f dB)\n", reductionDb);
        return 1;
    }
    std::printf ("Royal Chain Color anti-aliasing contract ok\n");
    return 0;
}
