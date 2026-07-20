#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariSessionHud.h>
#include "PluginProcessor.h"

class SattariMorphAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SattariMorphAudioProcessorEditor (SattariMorphAudioProcessor&);
    ~SattariMorphAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void knob (juce::Slider&, juce::Label&, const juce::String&, const juce::String& id, std::unique_ptr<SA>&);

    SattariMorphAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("morph") };
    sattari::ui::SessionHud sessionHud;

    juce::Label title, subtitle, robotCap;
    juce::ComboBox presetBox, modeBox, robotNoteBox;
    std::unique_ptr<CA> modeAtt, robotAtt;

    juce::Slider pitch, formant, drive, mix;
    juce::Label  pitchC, formantC, driveC, mixC;
    std::unique_ptr<SA> pitchA, formantA, driveA, mixA;

    // Harmony voice — dry in-key parallel voice.
    juce::Label    harmCaption;
    juce::ComboBox harmVoiceBox;
    std::unique_ptr<CA> harmVoiceAtt;
    juce::Slider   harmMix;
    std::unique_ptr<SA> harmMixA;

    // Key Echo section — in-key pitched delay.
    juce::Label    echoCaption;
    juce::ComboBox echoModeBox;
    std::unique_ptr<CA> echoModeAtt;
    juce::Slider echoTime, echoFeedback, echoMix;
    juce::Label  echoTimeC, echoFeedbackC, echoMixC;
    std::unique_ptr<SA> echoTimeA, echoFeedbackA, echoMixA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariMorphAudioProcessorEditor)
};
