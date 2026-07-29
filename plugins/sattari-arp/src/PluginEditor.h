#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariKeyButton.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <sattari_audio_core/SattariStudio.h>
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
    sattari::studio::LookAndFeel lookAndFeel {
        sattari::brand::productAccent ("sattari-arp")
    };
    sattari::studio::Header header {
        "Sattari Arp", "GENERATIVE MIDI PERFORMANCE", accent
    };

    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager };

    juce::ComboBox preset, rate, pattern, velocityMode, userPresets;
    juce::Label presetLabel, rateLabel, patternLabel, velocityLabel;
    sattari::ui::KeyButton keyButton;
    juce::TextButton savePresetButton { "Save" };
    juce::TextButton midiLearnButton { "MIDI learn" };

    sattari::studio::MacroControl octaves { "Octaves" };
    sattari::studio::MacroControl ratchet { "Ratchet" };
    sattari::studio::MacroControl gate { "Gate" };
    sattari::studio::MacroControl swing { "Swing" };
    sattari::studio::MacroControl fixedVelocity { "Velocity" };
    sattari::studio::MacroControl humanize { "Humanize" };
    sattari::studio::MacroControl transpose { "Transpose", " st" };

    juce::ToggleButton latch { "Latch" };
    juce::ToggleButton inputThru { "Input thru" };
    juce::ToggleButton hostSync { "Host sync" };
    sattari::studio::MetricCard tempoMetric { "Tempo" };
    sattari::studio::MetricCard heldMetric { "Held notes" };

    std::unique_ptr<ComboAttachment> rateAttach, patternAttach, velocityModeAttach;
    std::unique_ptr<SliderAttachment> octavesAttach, ratchetAttach, gateAttach,
                                      swingAttach, fixedVelocityAttach,
                                      humanizeAttach, transposeAttach;
    std::unique_ptr<ButtonAttachment> latchAttach, inputThruAttach, hostSyncAttach;

    SattariArpAudioProcessor::UiState uiState;
    juce::Rectangle<int> workspaceArea, sequenceArea, keyArea, controlsArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariArpAudioProcessorEditor)
};
