#pragma once

#include <JuceHeader.h>

class RoyalChainAudioProcessor;

class VocalCrownCompass final : public juce::Component
{
public:
    explicit VocalCrownCompass (RoyalChainAudioProcessor& processorRef);
    void paint (juce::Graphics& g) override;

private:
    RoyalChainAudioProcessor& processor;
    void drawNode (juce::Graphics& g, juce::Point<float> centre, float value, juce::Colour colour, const juce::String& label);
};
