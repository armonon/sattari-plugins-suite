#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "SongKeyProcessor.h"

class SattariSongKeyAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                 private juce::Timer
{
public:
    explicit SattariSongKeyAudioProcessorEditor (SattariSongKeyAudioProcessor&);
    ~SattariSongKeyAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void setupCombo (juce::ComboBox&, const juce::StringArray&);

    SattariSongKeyAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("song-key") };

    juce::Label title, subtitle;
    juce::ToggleButton publishToggle { "Publish to Suite" }, manualToggle { "Manual Override" };
    juce::ComboBox manualRootBox, manualScaleBox;
    juce::TextButton rescanButton { "Re-scan" };

    std::unique_ptr<ButtonAttachment> publishAtt, manualAtt;
    std::unique_ptr<ComboAttachment>  rootAtt, scaleAtt;

    SattariSongKeyAudioProcessor::Detected snapshot;
    juce::Rectangle<int> keyArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariSongKeyAudioProcessorEditor)
};
