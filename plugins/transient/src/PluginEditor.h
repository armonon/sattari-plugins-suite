#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"

class SattariTransientAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SattariTransientAudioProcessorEditor (SattariTransientAudioProcessor&);
    ~SattariTransientAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void styleKnob (juce::Slider&);
    void styleCaption (juce::Label&, const juce::String&);

    SattariTransientAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("transient") };

    juce::Label title, subtitle;
    juce::Slider attack, sustain, output;
    juce::Label  attackCap, sustainCap, outputCap;
    std::array<std::unique_ptr<SliderAttachment>, 3> atts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariTransientAudioProcessorEditor)
};
