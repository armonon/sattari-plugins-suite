#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "PluginProcessor.h"

class VoxKeyOrb final : public juce::Component
{
public:
    void setState (const VoxKeyVoiceMidiMapper::State& newState);
    void paint (juce::Graphics& g) override;

private:
    VoxKeyVoiceMidiMapper::State state;
};

class SattariVoxKeyAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SattariVoxKeyAudioProcessorEditor (SattariVoxKeyAudioProcessor&);
    ~SattariVoxKeyAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void setupSlider (juce::Slider& slider, const juce::String& suffix = {});
    void setupCombo (juce::ComboBox& combo);
    void setupLabel (juce::Label& label, const juce::String& text, float size = 13.0f);
    void setupToggle (juce::ToggleButton& button);
    void updateMeters();
    void refreshUserPresets();
    void saveCurrentAsUserPreset();

    SattariVoxKeyAudioProcessor& audioProcessor;

    // Shared suite UI + cross-plugin preset system.
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("voxkey") };
    sattari::presets::SattariPresetManager presetManager;
    juce::ComboBox userPresets;
    juce::TextButton savePresetButton { "Save Preset" };

    juce::Label title, subtitle, inputLabel, routingLabel, detectedLabel, outputLabel, confidenceLabel;
    juce::ComboBox mode, key, scale;
    juce::Slider sensitivity, smoothness, confidence, octave, hold, release, velocity, pitchBendRange;
    juce::ToggleButton pitchBend, glide, bypass;
    juce::TextButton panicButton { "Panic" };
    VoxKeyOrb orb;

    std::unique_ptr<ComboAttachment> modeAttach, keyAttach, scaleAttach;
    std::unique_ptr<SliderAttachment> sensitivityAttach, smoothnessAttach, confidenceAttach, octaveAttach, holdAttach, releaseAttach, velocityAttach, pitchBendRangeAttach;
    std::unique_ptr<ButtonAttachment> pitchBendAttach, glideAttach, bypassAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariVoxKeyAudioProcessorEditor)
};
