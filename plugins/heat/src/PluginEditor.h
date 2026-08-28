#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "PluginProcessor.h"

class SattariHeatAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit SattariHeatAudioProcessorEditor (SattariHeatAudioProcessor&);
    ~SattariHeatAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void addKnob (Knob& k, const juce::String& paramId, const juce::String& caption,
                  const juce::String& suffix = {});
    void refreshUserPresets();
    void saveCurrentAsUserPreset();
    void showMidiLearnPopup();

    SattariHeatAudioProcessor& processor;

    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("heat") };
    sattari::presets::SattariPresetManager presetManager;

    void registerFactoryPresets();

    juce::Label title, subtitle, heatLabel;
    juce::ComboBox model, factoryPresets, userPresets;
    juce::TextButton savePresetButton { "Save Preset" };
    sattari::ui::SattariABRow abRow { presetManager };
    juce::ToggleButton bypass;
    juce::TextButton midiLearnButton { "MIDI Learn..." };
    std::unique_ptr<ComboAttachment> modelAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach;

    // 5 knobs (model is a combo).
    Knob drive, tone, bias, mix, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariHeatAudioProcessorEditor)
};
