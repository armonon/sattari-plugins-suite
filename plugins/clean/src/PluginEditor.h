#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"

class SattariCleanAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SattariCleanAudioProcessorEditor (SattariCleanAudioProcessor&);
    ~SattariCleanAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void styleKnob (juce::Slider&);
    void styleCaption (juce::Label&, const juce::String&);

    SattariCleanAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("clean") };

    juce::Label title, subtitle;
    juce::Slider amount, sensitivity, output;
    juce::Label  amountCap, sensCap, outputCap, grCap;
    std::array<std::unique_ptr<SA>, 3> atts;
    float grSmoothed = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariCleanAudioProcessorEditor)
};
