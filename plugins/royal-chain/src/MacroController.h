#pragma once

#include <JuceHeader.h>

struct RoyalMacros
{
    float clean = 0.5f;
    float tune = 0.0f;
    float control = 0.55f;
    float tone = 0.5f;
    float air = 0.45f;
    float smooth = 0.45f;
    float glow = 0.35f;
    float width = 0.25f;
    float space = 0.25f;
    float royal = 0.5f;
};

class MacroController
{
public:
    static RoyalMacros readMacros (juce::AudioProcessorValueTreeState& state);
    static void setParameterValue (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, float plainValue);
};
