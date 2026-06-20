#include "WidthModule.h"

void WidthModule::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    const int size = juce::jmax (128, (int) (0.040 * sr) + maxBlockSize + 8);
    leftDelay.assign ((size_t) size, 0.0f);
    rightDelay.assign ((size_t) size, 0.0f);
    reset();
}

void WidthModule::reset()
{
    std::fill (leftDelay.begin(), leftDelay.end(), 0.0f);
    std::fill (rightDelay.begin(), rightDelay.end(), 0.0f);
    writePos = 0;
    widthMeter = 0.0f;
}

void WidthModule::process (juce::AudioBuffer<float>& buffer, float width, float royal)
{
    if (buffer.getNumChannels() < 2 || leftDelay.empty())
        return;

    const float amount = juce::jlimit (0.0f, 1.0f, width * 0.85f + royal * 0.15f);
    const int delaySamples = juce::jlimit (1, (int) leftDelay.size() - 1, (int) (sr * (0.008 + 0.010 * amount)));
    float sidePower = 0.0f;

    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);
    const int size = (int) leftDelay.size();

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int readPos = (writePos + size - delaySamples) % size;
        const float dl = leftDelay[(size_t) readPos];
        const float dr = rightDelay[(size_t) readPos];
        const float inL = l[i];
        const float inR = r[i];

        leftDelay[(size_t) writePos] = inL;
        rightDelay[(size_t) writePos] = inR;
        writePos = (writePos + 1) % size;

        const float mid = 0.5f * (inL + inR);
        float side = 0.5f * (inL - inR);
        const float doubledSide = 0.5f * ((inL - dr) - (inR - dl));
        side = side * (1.0f + amount * 0.55f) + doubledSide * amount * 0.45f;
        sidePower += std::abs (side);

        l[i] = mid + side;
        r[i] = mid - side;
    }

    widthMeter += 0.08f * (juce::jlimit (0.0f, 1.0f, sidePower / (float) juce::jmax (1, buffer.getNumSamples()) * 3.0f) - widthMeter);
}
