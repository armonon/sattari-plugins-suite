#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"
#include <vector>

class SattariEQAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SattariEQAudioProcessorEditor (SattariEQAudioProcessor&);
    ~SattariEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;

    juce::Slider& addKnob (const juce::String& id, const juce::String& caption, const juce::String& suffix = {});

    SattariEQAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("eq") };

    juce::Label title, subtitle;
    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::Label>  caps;
    std::vector<std::unique_ptr<SA>> atts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariEQAudioProcessorEditor)
};
