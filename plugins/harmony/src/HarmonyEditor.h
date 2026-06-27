#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "HarmonyProcessor.h"
#include <array>

class SattariHarmonyAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                 private juce::Timer
{
public:
    explicit SattariHarmonyAudioProcessorEditor (SattariHarmonyAudioProcessor&);
    ~SattariHarmonyAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    static constexpr int kNumVoices = SattariHarmonyAudioProcessor::kNumVoices;

    void setupSlider (juce::Slider&, const juce::String& suffix = {});
    void setupCombo  (juce::ComboBox&, const juce::StringArray& items);
    void timerCallback() override;
    void drawVoiceFan (juce::Graphics&, juce::Rectangle<int> area);
    void refreshUserPresets();
    void saveCurrentAsUserPreset();

    SattariHarmonyAudioProcessor& processor;

    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("harmony") };
    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager };

    juce::Label title, subtitle, keyStatus, inputMeterLabel, outputMeterLabel;
    juce::ComboBox userPresets, rootBox, scaleBox;
    juce::TextButton savePresetButton { "Save Preset" }, midiLearnButton { "MIDI Learn..." };
    juce::ToggleButton followKeyToggle { "Follow Song Key" };

    juce::Slider inputSlider, outputSlider, mixSlider, tuneSlider, spreadSlider, humanizeSlider;

    struct VoiceStrip
    {
        juce::ToggleButton on;
        juce::ComboBox interval;
        juce::Slider level, pan, formant;
    };
    std::array<VoiceStrip, kNumVoices> voices;

    std::unique_ptr<ComboAttachment>  rootAtt, scaleAtt;
    std::unique_ptr<ButtonAttachment> followAtt;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAtts;
    std::vector<std::unique_ptr<ComboAttachment>>  comboAtts;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAtts;

    SattariHarmonyAudioProcessor::Snapshot snapshot;
    juce::Rectangle<int> fanArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariHarmonyAudioProcessorEditor)
};
