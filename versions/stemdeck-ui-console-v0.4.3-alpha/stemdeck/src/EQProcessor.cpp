#include "EQProcessor.h"

namespace
{
float makeOnePoleCoeff (double sampleRate, float hz)
{
    return juce::jlimit (0.001f, 0.999f, 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * hz / (float) sampleRate));
}
}

void EQProcessor::prepare (double sampleRate, int)
{
    lowCoeff = makeOnePoleCoeff (sampleRate, 220.0f);
    midLowCoeff = makeOnePoleCoeff (sampleRate, 350.0f);
    midHighCoeff = makeOnePoleCoeff (sampleRate, 3200.0f);
    highCoeff = makeOnePoleCoeff (sampleRate, 6000.0f);
    reset();
}

void EQProcessor::reset()
{
    for (auto& s : state)
        s = {};
}

float EQProcessor::dbToGain (float db)
{
    return juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 12.0f, db));
}

float EQProcessor::processOne (float input, ChannelState& s, float lowGain, float midGain, float highGain)
{
    s.lowState += lowCoeff * (input - s.lowState);
    s.midLowState += midLowCoeff * (input - s.midLowState);
    s.midHighState += midHighCoeff * (input - s.midHighState);
    s.highState += highCoeff * (input - s.highState);

    const float low = s.lowState;
    const float mid = s.midHighState - s.midLowState;
    const float high = input - s.highState;
    const float dryMid = input - low - high;

    return low * lowGain + (0.65f * mid + 0.35f * dryMid) * midGain + high * highGain;
}

void EQProcessor::processSample (float& left, float& right, float lowDb, float midDb, float highDb)
{
    const float lowGain = dbToGain (lowDb);
    const float midGain = dbToGain (midDb);
    const float highGain = dbToGain (highDb);
    left = processOne (left, state[0], lowGain, midGain, highGain);
    right = processOne (right, state[1], lowGain, midGain, highGain);
}
