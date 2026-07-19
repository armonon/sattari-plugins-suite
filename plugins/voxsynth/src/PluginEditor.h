#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"

class SattariVoxSynthAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                  private juce::Timer
{
public:
    explicit SattariVoxSynthAudioProcessorEditor (SattariVoxSynthAudioProcessor&);
    ~SattariVoxSynthAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void knob (juce::Slider&, juce::Label&, const juce::String&, const juce::String& id, std::unique_ptr<SA>&);

    SattariVoxSynthAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("voxsynth") };

    juce::Label title, subtitle, readout;
    juce::ComboBox modeBox, osc1Box, osc2Box;
    std::unique_ptr<CA> modeAtt, osc1Att, osc2Att;
    juce::ToggleButton percBtn { "Percussion" };
    std::unique_ptr<BA> percAtt;

    juce::Slider mix, detune, glide, cutoff, filtAmt, reso, quantize, level;
    juce::Label  mixC, detuneC, glideC, cutoffC, filtAmtC, resoC, quantizeC, levelC;
    std::unique_ptr<SA> mixA, detuneA, glideA, cutoffA, filtAmtA, resoA, quantizeA, levelA;

    float glow = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariVoxSynthAudioProcessorEditor)
};
