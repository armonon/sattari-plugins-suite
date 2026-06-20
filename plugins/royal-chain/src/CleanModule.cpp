#include "CleanModule.h"

void CleanModule::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    // Fixed times; same as the original inline coefficients.
    envelope.prepare (sr, 3.0f, 80.0f);
    reset();
}

void CleanModule::reset()
{
    envelope.reset();
    hpX[0] = hpX[1] = hpY[0] = hpY[1] = 0.0f;
}

void CleanModule::process (juce::AudioBuffer<float>& buffer, float inputGainDb, float clean, float royal)
{
    const float inputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, inputGainDb));
    const float cutoff = 60.0f + 70.0f * juce::jlimit (0.0f, 1.0f, clean);
    const float rc = 1.0f / (2.0f * juce::MathConstants<float>::pi * cutoff);
    const float dt = 1.0f / (float) sr;
    const float a = rc / (rc + dt);
    const float gateThreshold = juce::Decibels::decibelsToGain (-58.0f + 18.0f * clean - 4.0f * royal);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float detector = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            detector += std::abs (buffer.getSample (ch, i));
        detector /= (float) juce::jmax (1, buffer.getNumChannels());

        const float env = envelope.process (detector);

        const float gate = env < gateThreshold ? juce::jmap (env, 0.0f, gateThreshold, 0.25f, 1.0f) : 1.0f;
        const float gateGain = juce::jlimit (0.18f, 1.0f, gate + (1.0f - clean) * 0.30f);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const int stateCh = juce::jmin (ch, 1);
            const float x = buffer.getSample (ch, i) * inputGain;
            const float y = a * (hpY[stateCh] + x - hpX[stateCh]);
            hpX[stateCh] = x;
            hpY[stateCh] = y;
            buffer.setSample (ch, i, y * gateGain);
        }
    }
}
