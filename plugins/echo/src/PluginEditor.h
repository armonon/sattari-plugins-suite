#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"
#include <vector>

class SattariEchoAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit SattariEchoAudioProcessorEditor (SattariEchoAudioProcessor&);
    ~SattariEchoAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void addKnob (const juce::String& id, const juce::String& cap, const juce::String& suffix = {});

    SattariEchoAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("echo") };

    juce::Label title, subtitle, tempoLabel, divCap;
    juce::ComboBox div;
    juce::ToggleButton syncBtn { "SYNC" }, pingBtn { "PING-PONG" };
    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::Label>  caps;
    std::vector<std::unique_ptr<SA>> atts;
    std::unique_ptr<CA> divAtt;
    std::unique_ptr<BA> syncAtt, pingAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariEchoAudioProcessorEditor)
};
