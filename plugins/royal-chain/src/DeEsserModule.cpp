#include "DeEsserModule.h"

void DeEsserModule::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void DeEsserModule::reset()
{
    lp[0] = lp[1] = 0.0f;
    essEnv = 0.0f;
    sibilance = 0.0f;
}

void DeEsserModule::process (juce::AudioBuffer<float>& buffer, float smooth, float royal)
{
    const float coeff = std::exp (-2.0f * juce::MathConstants<float>::pi * 5800.0f / (float) sr);
    const float threshold = juce::Decibels::decibelsToGain (-33.0f + (1.0f - smooth) * 12.0f);
    const float depth = juce::jlimit (0.0f, 0.65f, smooth * 0.52f + royal * 0.10f);
    float maxEss = 0.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float highDetector = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const int stateCh = juce::jmin (ch, 1);
            const float x = buffer.getSample (ch, i);
            lp[stateCh] = coeff * lp[stateCh] + (1.0f - coeff) * x;
            highDetector += std::abs (x - lp[stateCh]);
        }
        highDetector /= (float) juce::jmax (1, buffer.getNumChannels());
        essEnv += 0.05f * (highDetector - essEnv);
        const float over = juce::jlimit (0.0f, 1.0f, (essEnv - threshold) / (threshold + 1.0e-8f));
        const float atten = 1.0f - over * depth;
        maxEss = juce::jmax (maxEss, over);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const int stateCh = juce::jmin (ch, 1);
            const float x = buffer.getSample (ch, i);
            const float high = x - lp[stateCh];
            buffer.setSample (ch, i, lp[stateCh] + high * atten);
        }
    }

    sibilance += 0.12f * (maxEss - sibilance);
}
