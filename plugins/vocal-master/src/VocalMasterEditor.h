#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "VocalMasterProcessor.h"
#include <array>

class SattariVocalMasterAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                     private juce::Timer
{
public:
    explicit SattariVocalMasterAudioProcessorEditor (SattariVocalMasterAudioProcessor&);
    ~SattariVocalMasterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void setupSlider (juce::Slider&, const juce::String& name, const juce::String& suffix = {});
    void timerCallback() override;
    void drawReduction (juce::Graphics&, juce::Rectangle<int> area);
    void refreshUserPresets();
    void saveCurrentAsUserPreset();

    SattariVocalMasterAudioProcessor& processor;

    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("vocal-master") };
    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager };

    juce::Label title, subtitle, inputMeterLabel, outputMeterLabel;
    juce::ComboBox userPresets;
    juce::TextButton savePresetButton { "Save Preset" }, midiLearnButton { "MIDI Learn..." };

    juce::Slider inputSlider, isolateSlider, denoiseSlider, deessSlider, toneSlider,
                 airSlider, levelSlider, mixSlider, outputSlider;

    std::vector<std::unique_ptr<SliderAttachment>> sliderAtts;

    float deEssGr = 0.0f, gateGr = 0.0f;
    juce::Rectangle<int> reductionArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariVocalMasterAudioProcessorEditor)
};
