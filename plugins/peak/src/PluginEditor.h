#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"

class SattariPeakAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit SattariPeakAudioProcessorEditor (SattariPeakAudioProcessor&);
    ~SattariPeakAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void drawStat (juce::Graphics&, juce::Rectangle<int>, const juce::String& label,
                   const juce::String& value, juce::Colour accent);

    SattariPeakAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("peak") };

    juce::Label title, subtitle;
    juce::Label pushCap, ceilCap, relCap, lookCap, targetCap;
    juce::Slider push, ceiling, release, lookahead, targetLufs;
    juce::ToggleButton truePeak { "True-Peak" }, autoLoud { "Target Loudness" };

    std::unique_ptr<SliderAttachment> pushAtt, ceilAtt, relAtt, lookAtt, targetAtt;
    std::unique_ptr<ButtonAttachment> tpAtt, autoAtt;

    SattariPeakAudioProcessor::Readout r;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariPeakAudioProcessorEditor)
};
