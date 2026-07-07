#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"

class SattariDoubleAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SattariDoubleAudioProcessorEditor (SattariDoubleAudioProcessor&);
    ~SattariDoubleAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void styleKnob (juce::Slider&);
    void styleCaption (juce::Label&, const juce::String&);

    SattariDoubleAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("double") };

    juce::Label title, subtitle;
    juce::Slider detune, timing, drift, width, level, output;
    juce::Label  detuneCap, timingCap, driftCap, widthCap, levelCap, outputCap;
    std::array<std::unique_ptr<SliderAttachment>, 6> atts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariDoubleAudioProcessorEditor)
};
