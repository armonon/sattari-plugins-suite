#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "RhythmGenieProcessor.h"

class SattariRhythmGenieAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                     private juce::Timer
{
public:
    explicit SattariRhythmGenieAudioProcessorEditor (SattariRhythmGenieAudioProcessor&);
    ~SattariRhythmGenieAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void setupSlider (juce::Slider&);
    void timerCallback() override;
    void drawGrid (juce::Graphics&, juce::Rectangle<int> area);
    void refreshUserPresets();
    void saveCurrentAsUserPreset();

    SattariRhythmGenieAudioProcessor& processor;

    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("rhythm-genie") };
    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager };

    juce::Label title, subtitle, transportStatus;
    juce::ComboBox styleBox, userPresets;
    juce::TextButton savePresetButton { "Save Preset" }, midiLearnButton { "MIDI Learn..." };
    juce::Slider densitySlider, swingSlider, velocitySlider, gateSlider, accentSlider, humanizeSlider;
    std::array<juce::ToggleButton, sattari::rhythm::kNumLanes> laneToggles;

    std::unique_ptr<ComboAttachment> styleAtt;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAtts;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAtts;

    juce::Rectangle<int> gridArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariRhythmGenieAudioProcessorEditor)
};
