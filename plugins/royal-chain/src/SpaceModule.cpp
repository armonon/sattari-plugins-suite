#include "SpaceModule.h"

void SpaceModule::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    const int size = juce::jmax (1024, (int) (0.75 * sr) + maxBlockSize + 8);
    delayL.assign ((size_t) size, 0.0f);
    delayR.assign ((size_t) size, 0.0f);
    reset();
}

void SpaceModule::reset()
{
    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    writePos = 0;
}

void SpaceModule::process (juce::AudioBuffer<float>& buffer, float space, float royal)
{
    if (delayL.empty())
        return;

    const float amount = juce::jlimit (0.0f, 1.0f, space * 0.88f + royal * 0.12f);
    const float mix = amount * 0.24f;
    const float feedback = 0.18f + amount * 0.22f;
    const int size = (int) delayL.size();
    const int delayA = juce::jlimit (1, size - 1, (int) (0.115 * sr));
    const int delayB = juce::jlimit (1, size - 1, (int) (0.173 * sr));

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int readA = (writePos + size - delayA) % size;
        const int readB = (writePos + size - delayB) % size;
        const float wetL = delayL[(size_t) readA];
        const float wetR = delayR[(size_t) readB];
        const float inL = buffer.getSample (0, i);
        const float inR = buffer.getNumChannels() > 1 ? buffer.getSample (1, i) : inL;

        delayL[(size_t) writePos] = juce::jlimit (-1.0f, 1.0f, inL + wetR * feedback);
        delayR[(size_t) writePos] = juce::jlimit (-1.0f, 1.0f, inR + wetL * feedback);
        writePos = (writePos + 1) % size;

        buffer.setSample (0, i, inL * (1.0f - mix) + wetL * mix);
        if (buffer.getNumChannels() > 1)
            buffer.setSample (1, i, inR * (1.0f - mix) + wetR * mix);
    }
}
