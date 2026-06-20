#include "ColorModule.h"

void ColorModule::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void ColorModule::reset() {}

void ColorModule::process (juce::AudioBuffer<float>& buffer, float glow, float royal)
{
    juce::ignoreUnused (sr);
    const float drive = 1.0f + 1.75f * juce::jlimit (0.0f, 1.0f, glow) + 0.65f * royal;
    const float mix = juce::jlimit (0.0f, 0.65f, glow * 0.42f + royal * 0.12f);
    const float compensate = 1.0f / (1.0f + 0.25f * (drive - 1.0f));

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float dry = data[i];
            const float wet = std::tanh (dry * drive) * compensate;
            data[i] = dry + mix * (wet - dry);
        }
    }
}
