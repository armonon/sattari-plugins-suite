#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariKeyButton.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <sattari_audio_core/SattariReleaseUI.h>
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

    void timerCallback() override;
    void setupCombo (juce::ComboBox&, const juce::String&);
    void setupFieldLabel (juce::Label&, const juce::String&);
    void applyPreset (int presetIndex);
    void setParameterValue (const juce::String& parameterID, float value);
    void refreshUserPresets();
    void saveCurrentAsUserPreset();
    void paintPatternWorkspace (juce::Graphics&);

    SattariArpAudioProcessor& audioProcessor;
    const juce::Colour accent {
        sattari::brand::productAccent ("sattari-arp")
    };
    sattari::releaseui::LookAndFeel lookAndFeel {
        sattari::brand::productAccent ("sattari-arp")
    };
    sattari::releaseui::Header header {
        "Sattari Arp", "MIDI arpeggiator", accent
    };

    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager, true };

    juce::ComboBox preset, rate, pattern, velocityMode, userPresets;
    juce::Label rateLabel, patternLabel, velocityLabel;
    sattari::ui::KeyButton keyButton;
    sattari::releaseui::IconButton savePresetButton { sattari::releaseui::Icon::save, "Save preset" };
    juce::TextButton midiLearnButton { "MIDI learn" };

    sattari::releaseui::MacroControl octaves { "Octaves" };
    sattari::releaseui::MacroControl ratchet { "Ratchet" };
    sattari::releaseui::MacroControl gate { "Gate" };
    sattari::releaseui::MacroControl swing { "Swing" };
    sattari::releaseui::MacroControl fixedVelocity { "Velocity" };
    sattari::releaseui::MacroControl humanize { "Humanize" };
    sattari::releaseui::MacroControl transpose { "Transpose", " st" };

    juce::ToggleButton latch { "Latch" };
    juce::ToggleButton inputThru { "Input thru" };
    juce::ToggleButton hostSync { "Host sync" };
    juce::ToggleButton followSwing { "Follow groove" };
    juce::ToggleButton followEnergy { "Follow energy" };
    sattari::releaseui::Metric tempoMetric { "Tempo" };
    sattari::releaseui::Metric heldMetric { "Held notes" };
    sattari::releaseui::Metric compassMetric { "Compass" };

    std::unique_ptr<ComboAttachment> rateAttach, patternAttach, velocityModeAttach;
    std::unique_ptr<SliderAttachment> octavesAttach, ratchetAttach, gateAttach,
                                      swingAttach, fixedVelocityAttach,
                                      humanizeAttach, transposeAttach;
    std::unique_ptr<ButtonAttachment> latchAttach, inputThruAttach, hostSyncAttach,
                                      followSwingAttach, followEnergyAttach;

    SattariArpAudioProcessor::UiState uiState;
    juce::Rectangle<int> workspaceArea, sequenceArea, keyArea, controlsArea;
    juce::TooltipWindow tooltip { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariArpAudioProcessorEditor)
};
