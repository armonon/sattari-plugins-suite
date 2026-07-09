#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"
#include <vector>

class SattariRiserAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SattariRiserAudioProcessorEditor (SattariRiserAudioProcessor&);
    ~SattariRiserAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void addKnob (const juce::String& id, const juce::String& cap, const juce::String& suffix = {});

    SattariRiserAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("riser") };

    juce::Label title, subtitle, tempoLabel, modeCap, lengthCap;
    juce::ComboBox modeBox, lengthBox;
    juce::ToggleButton armButton { "ARM" }, hostTrigButton { "HOST TRIG" };

    // A slim build-progress meter under the header.
    juce::Rectangle<float> meterBounds;

    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::Label>  caps;
    std::vector<std::unique_ptr<SA>> atts;
    std::unique_ptr<CA> modeAtt, lengthAtt;
    std::unique_ptr<BA> armAtt, hostTrigAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariRiserAudioProcessorEditor)
};
