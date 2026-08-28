#pragma once

#include <JuceHeader.h>

class EQProcessor
{
public:
    void prepare (double sampleRate, int channels);
    void reset();
    void processSample (float& left, float& right, float lowDb, float midDb, float highDb);

private:
    struct ChannelState
    {
        float lowState = 0.0f;
        float midLowState = 0.0f;
        float midHighState = 0.0f;
        float highState = 0.0f;
    };

    float lowCoeff = 0.0f;
    float midLowCoeff = 0.0f;
    float midHighCoeff = 0.0f;
    float highCoeff = 0.0f;
    std::array<ChannelState, 2> state;

    static float dbToGain (float db);
    float processOne (float input, ChannelState& s, float lowGain, float midGain, float highGain);
};
