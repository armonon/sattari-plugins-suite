#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"

// Rack UI: one row per slot (module chooser + bypass), so the chain is assembled
// top-to-bottom in the order it processes.
class SattariMixAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SattariMixAudioProcessorEditor (SattariMixAudioProcessor&);
    ~SattariMixAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    SattariMixAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("mix") };

    juce::Label title, subtitle;
    struct SlotRow
    {
        juce::Label    number;
        juce::ComboBox chooser;
        juce::ToggleButton bypass { "Bypass" };
        std::unique_ptr<CA> chooserAtt;
        std::unique_ptr<BA> bypassAtt;
    };
    std::array<SlotRow, SattariMixAudioProcessor::kNumSlots> slots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariMixAudioProcessorEditor)
};
