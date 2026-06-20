#pragma once

#include <JuceHeader.h>

class MixerSection final : public juce::Component
{
public:
    void paint (juce::Graphics& g) override;
    void resized() override {}
};
