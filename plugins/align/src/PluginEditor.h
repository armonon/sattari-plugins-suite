#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "PluginProcessor.h"

class SattariAlignAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                private juce::Timer
{
public:
    explicit SattariAlignAudioProcessorEditor (SattariAlignAudioProcessor&);
    ~SattariAlignAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;

    SattariAlignAudioProcessor& audioProcessor;

    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("align") };
    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager };

    juce::Label title, subtitle, offsetReadout, confidenceLabel, guideStatus;
    juce::Slider searchRange, sensitivity, smoothing, manualOffset;
    juce::Label searchCaption, sensitivityCaption, smoothingCaption, manualCaption, algoCaption;
    juce::ComboBox algorithm;
    juce::ToggleButton autoAlign { "Auto Align" }, lock { "Lock" }, bypass { "Bypass" };
    juce::TextButton midiLearnButton { "MIDI Learn..." };
    juce::ComboBox userPresets;
    juce::TextButton savePresetButton { "Save Preset" };

    std::unique_ptr<SliderAttachment> searchAttach, sensAttach, smoothAttach, manualAttach;
    std::unique_ptr<ButtonAttachment> autoAttach, lockAttach, bypassAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> algoAttach;

    void styleKnob (juce::Slider& s, bool feature = false);
    void styleCaption (juce::Label& l, const juce::String& text);
    void refreshUserPresets();
    void saveCurrentAsUserPreset();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariAlignAudioProcessorEditor)
};
