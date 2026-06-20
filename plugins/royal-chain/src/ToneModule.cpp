#include "ToneModule.h"

void ToneModule::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void ToneModule::reset()
{
    mudLp[0] = mudLp[1] = airLp[0] = airLp[1] = 0.0f;
}

void ToneModule::process (juce::AudioBuffer<float>& buffer, float tone, float air, float royal)
{
    const float mudCoeff = std::exp (-2.0f * juce::MathConstants<float>::pi * 280.0f / (float) sr);
    const float airCoeff = std::exp (-2.0f * juce::MathConstants<float>::pi * 5200.0f / (float) sr);
    const float deMud = juce::jlimit (0.0f, 0.45f, (1.0f - tone) * 0.34f + royal * 0.08f);
    const float body = juce::jlimit (0.0f, 0.22f, juce::jmax (0.0f, tone - 0.55f) * 0.36f);
    const float airLift = juce::jlimit (0.0f, 0.42f, air * 0.34f + royal * 0.08f);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const int stateCh = juce::jmin (ch, 1);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float x = buffer.getSample (ch, i);
            mudLp[stateCh] = mudCoeff * mudLp[stateCh] + (1.0f - mudCoeff) * x;
            airLp[stateCh] = airCoeff * airLp[stateCh] + (1.0f - airCoeff) * x;
            const float high = x - airLp[stateCh];
            const float y = x - deMud * mudLp[stateCh] + body * mudLp[stateCh] + airLift * high;
            buffer.setSample (ch, i, y);
        }
    }
}
