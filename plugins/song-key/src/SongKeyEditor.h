#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAnalysis.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariSessionHud.h>
#include "SongKeyProcessor.h"

class SattariSongKeyAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    explicit SattariSongKeyAudioProcessorEditor (
        SattariSongKeyAudioProcessor&);
    ~SattariSongKeyAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ComboAttachment =
        juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment =
        juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void updateSnapshot();
    void updateManualControls();
    void setupCombo (
        juce::ComboBox&, const juce::StringArray&,
        const juce::String& title);
    juce::String currentRoot() const;
    juce::String currentMode() const;

    static juce::String rootName (int pitchClass);

    SattariSongKeyAudioProcessor& audioProcessor;
    const juce::Colour accent {
        sattari::brand::productAccent ("song-key")
    };
    sattari::studio::LookAndFeel lookAndFeel {
        sattari::brand::productAccent ("song-key")
    };
    sattari::studio::Header header {
        "Sattari Key", "MASTER KEY / SESSION AUTHORITY", accent
    };

    sattari::ui::SessionHud sessionHud;
    juce::ToggleButton publishToggle { "Publish to suite" };
    juce::TextButton rescanButton { "Re-scan" };
    juce::ToggleButton manualToggle { "Manual override" };
    juce::ComboBox manualRootBox;
    juce::ComboBox manualScaleBox;
    std::unique_ptr<ButtonAttachment> publishAttachment;
    std::unique_ptr<ButtonAttachment> manualAttachment;
    std::unique_ptr<ComboAttachment> rootAttachment;
    std::unique_ptr<ComboAttachment> scaleAttachment;

    sattari::studio::analysis::Metric confidenceMetric {
        "Confidence", accent
    };
    sattari::studio::analysis::Metric tuningMetric {
        "Reference offset", accent
    };
    sattari::studio::analysis::Metric engineMetric {
        "Detection engine", accent
    };

    SattariSongKeyAudioProcessor::Detected snapshot;
    juce::Rectangle<int> keyArea;
    juce::Rectangle<int> manualArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (
        SattariSongKeyAudioProcessorEditor)
};
