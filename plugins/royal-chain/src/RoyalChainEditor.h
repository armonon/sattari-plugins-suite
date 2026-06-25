#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "RoyalChainProcessor.h"
#include "VocalCrownCompass.h"

class RoyalChainAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit RoyalChainAudioProcessorEditor (RoyalChainAudioProcessor&);
    ~RoyalChainAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    RoyalChainAudioProcessor& processor;

    // Shared suite UI + cross-plugin preset system.
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("royal-chain") };
    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager };

    juce::Label title, subtitle, inputMeterLabel, outputMeterLabel;
    juce::ComboBox presetBox, styleBox, userPresets;
    juce::TextButton savePresetButton { "Save Preset" };
    juce::Slider inputSlider, outputSlider;
    juce::Slider cleanSlider, tuneSlider, controlSlider, toneSlider, airSlider, smoothSlider, glowSlider, widthSlider, spaceSlider, royalSlider;
    VocalCrownCompass compass;

    std::unique_ptr<ComboAttachment> styleAttachment;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;

    void setupSlider (juce::Slider& slider, const juce::String& name, const juce::String& suffix = {});
    void setupCombo (juce::ComboBox& combo);
    void applyPreset (int presetIndex);
    void refreshUserPresets();
    void saveCurrentAsUserPreset();
    void timerCallback() override;
    void drawChainFlow (juce::Graphics& g, juce::Rectangle<int> area);
    void drawSliderLabel (juce::Graphics& g, const juce::Slider& slider);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoyalChainAudioProcessorEditor)
};
