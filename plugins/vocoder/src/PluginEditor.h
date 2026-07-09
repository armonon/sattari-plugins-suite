#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariKeyButton.h>
#include "PluginProcessor.h"
#include <vector>

class SattariVocoderAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                 private juce::Timer
{
public:
    explicit SattariVocoderAudioProcessorEditor (SattariVocoderAudioProcessor&);
    ~SattariVocoderAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void setKeyParam (const juce::String& id, float value);
    juce::Slider& addKnob (const juce::String& id, const juce::String& caption, const juce::String& suffix = {});
    juce::ComboBox& addCombo (const juce::String& id, const juce::String& caption, const juce::StringArray& items);

    SattariVocoderAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("vocoder") };

    juce::Label title, subtitle;
    sattari::ui::KeyButton keyButton;

    juce::OwnedArray<juce::Slider>   knobs;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::Label>    caps;
    std::vector<std::unique_ptr<SA>> knobAtts;
    std::vector<std::unique_ptr<CA>> comboAtts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariVocoderAudioProcessorEditor)
};
