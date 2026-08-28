#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariKeyButton.h>
#include "PluginProcessor.h"

class SattariScalerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                private juce::Timer
{
public:
    explicit SattariScalerAudioProcessorEditor (SattariScalerAudioProcessor&);
    ~SattariScalerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void setKeyParam (const juce::String& id, float value);

    SattariScalerAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("scaler") };

    juce::Label title, subtitle, chordCap, octaveCap;
    sattari::ui::KeyButton keyButton;
    juce::ComboBox chord;
    juce::Slider octave;
    std::unique_ptr<ComboAttachment>  chordAtt;
    std::unique_ptr<SliderAttachment> octaveAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariScalerAudioProcessorEditor)
};
