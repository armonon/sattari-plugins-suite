#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "PluginProcessor.h"

class SattariBusAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit SattariBusAudioProcessorEditor (SattariBusAudioProcessor&);
    ~SattariBusAudioProcessorEditor() override;

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
    void addKnob (Knob& k, const juce::String& paramId, const juce::String& caption, const juce::String& suffix = {});
    void refreshUserPresets();
    void saveCurrentAsUserPreset();

    SattariBusAudioProcessor& processor;

    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("bus") };
    sattari::presets::SattariPresetManager presetManager;

    void registerFactoryPresets();

    juce::Label title, subtitle, grLabel;
    juce::ComboBox mode, factoryPresets, userPresets;
    juce::TextButton savePresetButton { "Save Preset" };
    sattari::ui::SattariABRow abRow { presetManager };
    juce::ToggleButton mute, bypass, msMode;
    juce::TextButton midiLearnButton { "MIDI Learn..." };
    void showMidiLearnPopup();
    std::unique_ptr<ComboAttachment> modeAttach;
    std::unique_ptr<ButtonAttachment> muteAttach, bypassAttach, msModeAttach;

    // 17 channel-strip knobs.
    Knob inputTrim, hpf, lpf, low, mid, high, threshold, ratio, attack, release,
         makeup, drive, width, pan, mix, output, ceiling;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariBusAudioProcessorEditor)
};
