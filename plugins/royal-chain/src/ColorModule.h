#pragma once

#include <JuceHeader.h>

class ColorModule
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, float glow, float royal);

private:
    double sr = 44100.0;
};
