#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include "PluginProcessor.h"

class SattariWarpAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                 private juce::Timer
{
public:
    explicit SattariWarpAudioProcessorEditor (SattariWarpAudioProcessor&);
    ~SattariWarpAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void styleKnob (juce::Slider&, bool big = false);
    void styleCaption (juce::Label&, const juce::String&);

    SattariWarpAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("warp") };

    juce::Label title, subtitle;

    juce::Slider dose;                 // the big macro
    juce::Label  doseCaption;

    juce::Slider vowel, vowelAmt, robotFreq, robotAmt, drive, crush, stutter, input, output, mix;
    juce::ComboBox stutterRate;
    juce::Label vowelCap, vowelAmtCap, robotFreqCap, robotAmtCap, driveCap, crushCap,
                stutterCap, stutterRateCap, inputCap, outputCap, mixCap;

    juce::TextButton midiLearnButton { "MIDI Learn..." };

    std::unique_ptr<SliderAttachment> doseAtt, vowelAtt, vowelAmtAtt, robotFreqAtt, robotAmtAtt,
                                      driveAtt, crushAtt, stutterAtt, inputAtt, outputAtt, mixAtt;
    std::unique_ptr<ComboAttachment>  stutterRateAtt;

    float pulse = 0.0f;   // reactive spiral phase

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariWarpAudioProcessorEditor)
};
