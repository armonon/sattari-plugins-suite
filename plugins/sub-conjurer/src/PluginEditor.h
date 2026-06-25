#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "PluginProcessor.h"

class SubConjurerAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SubConjurerAudioProcessorEditor (SubConjurerAudioProcessor&);
    ~SubConjurerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    SubConjurerAudioProcessor& processor;

    // Shared suite UI + cross-plugin preset system.
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("sub-conjurer") };
    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager };

    juce::Label title, subtitle;
    juce::ComboBox preset, userPresets, mode, tuning, note;
    juce::TextButton savePresetButton;
    juce::Slider octave, amount, dry, richness, glide, saturation, attack, release, limiter;
    juce::ToggleButton subOnly;
    juce::TextButton midiLearnButton { "MIDI Learn..." };

    std::unique_ptr<ComboAttachment> modeAttach, tuningAttach, noteAttach;
    std::unique_ptr<SliderAttachment> octaveAttach, amountAttach, dryAttach, richnessAttach, glideAttach, saturationAttach, attackAttach, releaseAttach, limiterAttach;
    std::unique_ptr<ButtonAttachment> subOnlyAttach;

    void setupSlider (juce::Slider&, const juce::String& suffix = {});
    void setupCombo (juce::ComboBox&);
    void applyPreset (int presetIndex);
    void setParameterValue (const juce::String& parameterID, float value);

    // Shared preset system helpers.
    void refreshUserPresets();
    void saveCurrentAsUserPreset();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubConjurerAudioProcessorEditor)
};
