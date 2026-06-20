#pragma once

#include <JuceHeader.h>

class DeEsserModule
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, float smooth, float royal);
    float getSibilance() const { return sibilance; }

private:
    double sr = 44100.0;
    float lp[2] { 0.0f, 0.0f };
    float essEnv = 0.0f;
    float sibilance = 0.0f;
};
