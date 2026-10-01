#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <sattari_audio_core/SattariReleaseUI.h>
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
    sattari::releaseui::LookAndFeel lookAndFeel {
        sattari::brand::productAccent ("sub-conjurer")
    };
    sattari::releaseui::Header header {
        "Sub Conjurer", "Low-frequency synthesis", accent
    };

    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager, true };

    juce::ComboBox preset, userPresets, mode, tuning, note;
    juce::Label modeLabel, tuningLabel, noteLabel;
    sattari::releaseui::IconButton savePresetButton { sattari::releaseui::Icon::save, "Save preset" };
    juce::TextButton midiLearnButton { "MIDI learn" };

    sattari::releaseui::MacroControl amount { "Sub amount", {}, true };
    sattari::releaseui::MacroControl dry { "Dry blend" };
    sattari::releaseui::MacroControl richness { "Richness" };
    sattari::releaseui::MacroControl saturation { "Saturation" };
    sattari::releaseui::MacroControl octave { "Octave" };
    sattari::releaseui::MacroControl glide { "Glide" };
    sattari::releaseui::MacroControl attack { "Attack", " s" };
    sattari::releaseui::MacroControl release { "Release", " s" };
    sattari::releaseui::MacroControl limiter { "Ceiling" };

    juce::ToggleButton subOnly { "Sub only" };
    juce::ToggleButton followKey { "Follow Compass" };
    sattari::releaseui::Metric pitchMetric { "Generated pitch" };
    sattari::releaseui::Metric inputMetric { "Input" };
    sattari::releaseui::Metric outputMetric { "Output" };

    std::unique_ptr<ComboAttachment> modeAttach, tuningAttach, noteAttach;
    std::unique_ptr<SliderAttachment> octaveAttach, amountAttach, dryAttach,
                                      richnessAttach, glideAttach, saturationAttach,
                                      attackAttach, releaseAttach, limiterAttach;
    std::unique_ptr<ButtonAttachment> subOnlyAttach, followKeyAttach;

    SubConjurerAudioProcessor::UiState uiState;
    juce::Rectangle<int> workspaceArea, displayArea, primaryArea, detailArea;
    juce::TooltipWindow tooltip { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubConjurerAudioProcessorEditor)
};
