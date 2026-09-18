#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAnalysis.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariSessionHud.h>
#include "SongKeyProcessor.h"

class SattariSongKeyAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
#if JucePlugin_Enable_ARA
      public juce::AudioProcessorEditorARAExtension,
#endif
      public juce::FileDragAndDropTarget,
      private juce::Timer
{
public:
    explicit SattariSongKeyAudioProcessorEditor (
        SattariSongKeyAudioProcessor&);
    ~SattariSongKeyAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    using ComboAttachment =
        juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment =
        juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void updateSnapshot();
    void updateManualControls();
    void setupCombo (
        juce::ComboBox&, const juce::StringArray&,
        const juce::String& title);
    juce::String currentRoot() const;
    juce::String currentMode() const;
    void chooseSong();
    void beginFileAnalysis (const juce::File&);

    static juce::String rootName (int pitchClass);
    static juce::String camelotName (int pitchClass, bool minor);

    SattariSongKeyAudioProcessor& audioProcessor;
    const juce::Colour accent {
        sattari::brand::productAccent ("song-key")
    };
    sattari::studio::LookAndFeel lookAndFeel {
        sattari::brand::productAccent ("song-key")
    };
    sattari::studio::Header header {
        "Sattari Key", "SONG ANALYSIS / SUITE KEY", accent
    };

    sattari::ui::SessionHud sessionHud;
    juce::ToggleButton lockToggle { "Lock session" };
    juce::ToggleButton dynamicFollowToggle { "Dynamic key" };
    juce::Slider dynamicInterval;
    juce::Slider songContext;
    juce::Label songContextLabel;
    juce::TextButton chooseSongButton { "Choose song" };
    juce::TextButton scanClipButton { "Scan DAW clip" };
    juce::TextButton liveButton { "Listen live" };
    juce::TextButton rescanButton { "Re-scan" };
    juce::ToggleButton manualToggle { "Manual override" };
    juce::ComboBox manualRootBox;
    juce::ComboBox manualScaleBox;
    std::unique_ptr<ButtonAttachment> lockAttachment;
    std::unique_ptr<ButtonAttachment> dynamicFollowAttachment;
    std::unique_ptr<SliderAttachment> dynamicIntervalAttachment;
    std::unique_ptr<SliderAttachment> songContextAttachment;
    std::unique_ptr<ButtonAttachment> manualAttachment;
    std::unique_ptr<ComboAttachment> rootAttachment;
    std::unique_ptr<ComboAttachment> scaleAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;

    sattari::studio::analysis::Metric confidenceMetric {
        "Decision margin", accent
    };
    sattari::studio::analysis::Metric tuningMetric {
        "Reference offset", accent
    };
    sattari::studio::analysis::Metric tempoMetric {
        "Tempo", accent
    };
    sattari::studio::analysis::Metric alternativeMetric {
        "Second estimate", accent
    };

    SattariSongKeyAudioProcessor::Detected snapshot;
    bool dragActive = false;
    juce::Rectangle<int> sourceArea;
    juce::Rectangle<int> keyArea;
    juce::Rectangle<int> manualArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (
        SattariSongKeyAudioProcessorEditor)
};
