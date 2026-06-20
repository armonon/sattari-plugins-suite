#pragma once

#include <JuceHeader.h>

class SpaceModule
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, float space, float royal);

private:
    double sr = 44100.0;
    std::vector<float> delayL;
    std::vector<float> delayR;
    int writePos = 0;
};
