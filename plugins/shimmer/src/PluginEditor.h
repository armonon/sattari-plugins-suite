#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariKeyButton.h>
#include "PluginProcessor.h"
#include <vector>

class SattariShimmerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                 private juce::Timer
{
public:
    explicit SattariShimmerAudioProcessorEditor (SattariShimmerAudioProcessor&);
    ~SattariShimmerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void addKnob (const juce::String& id, const juce::String& cap, const juce::String& suffix = {});
    void setKeyParam (const juce::String& id, float value);

    SattariShimmerAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("shimmer") };

    juce::Label title, subtitle, intervalCap;
    juce::ComboBox interval;
    sattari::ui::KeyButton keyButton;
    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::Label>  caps;
    std::vector<std::unique_ptr<SA>> atts;
    std::unique_ptr<CA> intervalAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariShimmerAudioProcessorEditor)
};
