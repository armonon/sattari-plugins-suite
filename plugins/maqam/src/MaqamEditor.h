#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <sattari_audio_core/SattariKeyButton.h>
#include "MaqamProcessor.h"

class SattariMaqamAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SattariMaqamAudioProcessorEditor (SattariMaqamAudioProcessor&);
    ~SattariMaqamAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void setupSlider (juce::Slider&);
    void setupCombo  (juce::ComboBox&, const juce::StringArray&);
    void setKeyParam (const juce::String& id, float value);   // set an APVTS param by real value
    void timerCallback() override;
    void drawLadder (juce::Graphics&, juce::Rectangle<int> area);
    void refreshUserPresets();
    void saveCurrentAsUserPreset();

    SattariMaqamAudioProcessor& processor;

    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("maqam") };
    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager };

    juce::Label title, subtitle, keyStatus, inputMeterLabel, outputMeterLabel;
    juce::ComboBox userPresets;
    sattari::ui::KeyButton keyButton;   // one control: Follow Song Key / set root+maqam by hand
    juce::ToggleButton formantToggle { "Preserve Formants" }, lowLatencyToggle { "Low Latency" };
    juce::TextButton savePresetButton { "Save Preset" }, midiLearnButton { "MIDI Learn..." };
    juce::Slider strengthSlider, mixSlider;

    std::unique_ptr<ButtonAttachment> formantAtt, lowLatAtt;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAtts;

    SattariMaqamAudioProcessor::Snapshot snapshot;
    juce::Rectangle<int> ladderArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariMaqamAudioProcessorEditor)
};
