#include "FinishModule.h"

void FinishModule::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void FinishModule::reset()
{
    limiterGain = 1.0f;
    limiterMeter = 0.0f;
}

void FinishModule::process (juce::AudioBuffer<float>& buffer, float outputGainDb, float royal)
{
    const float outGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 12.0f, outputGainDb) + royal * 1.5f);
    const float ceiling = 0.94f - royal * 0.04f;
    float maxReduction = 0.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, i) * outGain));

        const float target = peak > ceiling ? ceiling / (peak + 1.0e-8f) : 1.0f;
        if (target < limiterGain)
            limiterGain = target;
        else
            limiterGain += 0.0015f * (1.0f - limiterGain);

        maxReduction = juce::jmax (maxReduction, 1.0f - limiterGain);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float y = std::tanh (buffer.getSample (ch, i) * outGain * limiterGain * (1.0f + royal * 0.08f));
            buffer.setSample (ch, i, juce::jlimit (-0.98f, 0.98f, y));
        }
    }

    juce::ignoreUnused (sr);
    limiterMeter += 0.10f * (juce::jlimit (0.0f, 1.0f, maxReduction * 6.0f) - limiterMeter);
}
