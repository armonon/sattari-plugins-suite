#pragma once
#include "PluginProcessor.h"

class SattariModularEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SattariModularEditor (SattariModularProcessor&);
    ~SattariModularEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    struct View;
    std::unique_ptr<View> view;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariModularEditor)
};
