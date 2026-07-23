#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "ChopProcessor.h"

// Sattari Chop editor — scaffold. The waveform + slice-marker surface (the product,
// §3) replaces the placeholder as the interaction grammar lands.
class SattariChopAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SattariChopAudioProcessorEditor (SattariChopAudioProcessor&);
    ~SattariChopAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    SattariChopAudioProcessor& processor;

    // Chop identity: a warm amber accent, distinct from the mint family.
    sattari::ui::SattariLookAndFeel lookAndFeel { 0xfff5a24b };

    juce::Label title, subtitle;
    juce::Slider sensitivitySlider, outputSlider;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAtts;

    juce::Rectangle<int> waveformArea, padArea;   // reserved for the editing surface

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariChopAudioProcessorEditor)
};
