#pragma once

#include <JuceHeader.h>

class FinishModule
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, float outputGainDb, float royal);
    float getLimiterMeter() const { return limiterMeter; }

private:
    double sr = 44100.0;
    float limiterGain = 1.0f;
    float limiterMeter = 0.0f;
};
