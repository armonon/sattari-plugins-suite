#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "PluginProcessor.h"

class Sattari808AudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit Sattari808AudioProcessorEditor (Sattari808AudioProcessor&);
    ~Sattari808AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;

    Sattari808AudioProcessor& processor;

    // Shared suite UI + cross-plugin preset system.
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("eightoheight") };
    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager };

    juce::Label title, subtitle, bodyReadout;
    juce::ComboBox preset, userPresets, wave;
    juce::TextButton savePresetButton;
    juce::Slider glide, decay, drive, subfold, blend, output;
    juce::ToggleButton snap;
    juce::TextButton midiLearnButton { "MIDI Learn..." };

    juce::Label waveLabel;
    juce::Label glideLabel, decayLabel, driveLabel, subfoldLabel, blendLabel, outputLabel;

    std::unique_ptr<ComboAttachment> waveAttach;
    std::unique_ptr<SliderAttachment> glideAttach, decayAttach, driveAttach, subfoldAttach, blendAttach, outputAttach;
    std::unique_ptr<ButtonAttachment> snapAttach;

    void setupSlider (juce::Slider&, const juce::String& suffix = {});
    void setupCombo (juce::ComboBox&);
    void styleCaption (juce::Label&, const juce::String& text);
    void applyPreset (int presetIndex);
    void setParameterValue (const juce::String& parameterID, float value);

    void refreshUserPresets();
    void saveCurrentAsUserPreset();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sattari808AudioProcessorEditor)
};
