#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"

class SattariMeterAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SattariMeterAudioProcessorEditor (SattariMeterAudioProcessor&);
    ~SattariMeterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void drawStat (juce::Graphics&, juce::Rectangle<int>, const juce::String& label,
                   const juce::String& value, juce::Colour accent);

    SattariMeterAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("meter") };

    juce::Label title, subtitle, targetCap;
    juce::Slider target;
    juce::TextButton resetBtn { "Reset Integrated" };
    std::unique_ptr<SliderAttachment> targetAtt;

    SattariMeterAudioProcessor::Readout r;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariMeterAudioProcessorEditor)
};
