#pragma once

#include <JuceHeader.h>

class WidthModule
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, float width, float royal);
    float getWidthMeter() const { return widthMeter; }

private:
    double sr = 44100.0;
    std::vector<float> leftDelay;
    std::vector<float> rightDelay;
    int writePos = 0;
    float widthMeter = 0.0f;
};
