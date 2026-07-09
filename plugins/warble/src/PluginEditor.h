#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"
#include <vector>

class SattariWarbleAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SattariWarbleAudioProcessorEditor (SattariWarbleAudioProcessor&);
    ~SattariWarbleAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;

    void addKnob (const juce::String& id, const juce::String& cap, const juce::String& suffix = {});

    SattariWarbleAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("warble") };

    juce::Label title, subtitle, ageCap;
    juce::Slider ageKnob;
    std::unique_ptr<SA> ageAtt;

    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::Label>  caps;
    std::vector<std::unique_ptr<SA>> atts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariWarbleAudioProcessorEditor)
};
