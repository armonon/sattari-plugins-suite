#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <sattari_audio_core/SattariStudio.h>
#include "PluginProcessor.h"

class SubConjurerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
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

    void timerCallback() override;
    void setupCombo (juce::ComboBox&, const juce::String&);
    void setupCaption (juce::Label&, const juce::String&);
    void applyPreset (int presetIndex);
    void setParameterValue (const juce::String& parameterID, float value);
    void refreshUserPresets();
    void saveCurrentAsUserPreset();
    void paintSubDisplay (juce::Graphics&);

    SubConjurerAudioProcessor& audioProcessor;
    const juce::Colour accent {
        sattari::brand::productAccent ("sub-conjurer")
    };
    sattari::studio::LookAndFeel lookAndFeel {
        sattari::brand::productAccent ("sub-conjurer")
    };
    sattari::studio::Header header {
        "Sub Conjurer", "LOW-END INSTRUMENT", accent
    };

    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager };

    juce::ComboBox preset, userPresets, mode, tuning, note;
    juce::Label modeLabel, tuningLabel, noteLabel;
    juce::TextButton savePresetButton { "Save" };
    juce::TextButton midiLearnButton { "MIDI learn" };

    sattari::studio::MacroControl amount { "Sub amount", {}, true };
    sattari::studio::MacroControl dry { "Dry blend" };
    sattari::studio::MacroControl richness { "Richness" };
    sattari::studio::MacroControl saturation { "Saturation" };
    sattari::studio::MacroControl octave { "Octave" };
    sattari::studio::MacroControl glide { "Glide" };
    sattari::studio::MacroControl attack { "Attack", " s" };
    sattari::studio::MacroControl release { "Release", " s" };
    sattari::studio::MacroControl limiter { "Ceiling" };

    juce::ToggleButton subOnly { "Sub only" };
    juce::ToggleButton followKey { "Follow Brain" };
    sattari::studio::MetricCard pitchMetric { "Generated pitch" };
    sattari::studio::MetricCard inputMetric { "Input" };
    sattari::studio::MetricCard outputMetric { "Output" };

    std::unique_ptr<ComboAttachment> modeAttach, tuningAttach, noteAttach;
    std::unique_ptr<SliderAttachment> octaveAttach, amountAttach, dryAttach,
                                      richnessAttach, glideAttach, saturationAttach,
                                      attackAttach, releaseAttach, limiterAttach;
    std::unique_ptr<ButtonAttachment> subOnlyAttach, followKeyAttach;

    SubConjurerAudioProcessor::UiState uiState;
    juce::Rectangle<int> workspaceArea, displayArea, primaryArea, detailArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubConjurerAudioProcessorEditor)
};
