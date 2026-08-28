#pragma once

#include <JuceHeader.h>

class FilterProcessor
{
public:
    void prepare (double sampleRate);
    void reset();
    void processSample (float& left, float& right, float control);

private:
    struct ChannelState
    {
        float low = 0.0f;
        float highSmooth = 0.0f;
    };

    double sr = 44100.0;
    std::array<ChannelState, 2> state;

    float processOne (float input, ChannelState& s, float control);
};
