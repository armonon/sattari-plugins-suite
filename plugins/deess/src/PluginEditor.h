#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"

class SattariDeEssAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SattariDeEssAudioProcessorEditor (SattariDeEssAudioProcessor&);
    ~SattariDeEssAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void styleKnob (juce::Slider&);
    void styleCaption (juce::Label&, const juce::String&);

    SattariDeEssAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("deess") };

    juce::Label title, subtitle;
    juce::Slider freq, threshold, range, output;
    juce::Label  freqCap, threshCap, rangeCap, outputCap, grCap;
    juce::ComboBox mode;
    juce::ToggleButton listen { "Listen" };

    std::unique_ptr<SliderAttachment> freqAtt, threshAtt, rangeAtt, outputAtt;
    std::unique_ptr<ComboAttachment>  modeAtt;
    std::unique_ptr<ButtonAttachment> listenAtt;

    float grSmoothed = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariDeEssAudioProcessorEditor)
};
