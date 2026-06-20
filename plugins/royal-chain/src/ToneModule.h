#pragma once

#include <JuceHeader.h>

class ToneModule
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, float tone, float air, float royal);

private:
    double sr = 44100.0;
    float mudLp[2] { 0.0f, 0.0f };
    float airLp[2] { 0.0f, 0.0f };
};
