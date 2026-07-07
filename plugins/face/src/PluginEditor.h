#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"

class SattariFaceAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SattariFaceAudioProcessorEditor (SattariFaceAudioProcessor&);
    ~SattariFaceAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void styleKnob (juce::Slider&);
    void styleCaption (juce::Label&, const juce::String&);

    SattariFaceAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("face") };

    juce::Label title, subtitle;
    juce::Slider pitch, formant, mix, output;
    juce::Label  pitchCap, formantCap, mixCap, outputCap;
    std::array<std::unique_ptr<SliderAttachment>, 4> atts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariFaceAudioProcessorEditor)
};
