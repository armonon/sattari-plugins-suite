#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <sattari_audio_core/SattariKeyButton.h>
#include "PluginProcessor.h"

class SattariArpAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit SattariArpAudioProcessorEditor (SattariArpAudioProcessor&);
    ~SattariArpAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    SattariArpAudioProcessor& audioProcessor;

    // Shared suite UI + cross-plugin preset system.
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("sattari-arp") };
    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager };

    juce::Label title, subtitle, rateLabel, patternLabel, velocityLabel, performanceLabel, scaleLabel;
    // Per-knob name captions (the knobs themselves were previously unnamed).
    juce::Label octavesLabel, ratchetLabel, gateLabel, swingLabel,
                fixedVelocityLabel, humanizeLabel, transposeLabel;
    juce::ComboBox preset, rate, pattern, velocityMode, userPresets;
    sattari::ui::KeyButton keyButton;   // one control: Follow Song Key / set root+scale by hand
    juce::TextButton savePresetButton { "Save Preset" };
    juce::Slider octaves, ratchet, gate, swing, fixedVelocity, humanize, transpose;
    juce::ToggleButton latch, inputThru, hostSync;
    juce::TextButton midiLearnButton { "MIDI Learn..." };

    std::unique_ptr<ComboAttachment> rateAttach, patternAttach, velocityModeAttach;
    std::unique_ptr<SliderAttachment> octavesAttach, ratchetAttach, gateAttach, swingAttach, fixedVelocityAttach, humanizeAttach, transposeAttach;
    std::unique_ptr<ButtonAttachment> latchAttach, inputThruAttach, hostSyncAttach;

    void timerCallback() override;
    void setupSlider (juce::Slider&, const juce::String& suffix = {});
    void setupCombo (juce::ComboBox&);
    void setupLabel (juce::Label&, const juce::String& text, float size = 14.0f);
    void applyPreset (int presetIndex);
    void setParameterValue (const juce::String& parameterID, float value);
    void refreshUserPresets();
    void saveCurrentAsUserPreset();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariArpAudioProcessorEditor)
};
