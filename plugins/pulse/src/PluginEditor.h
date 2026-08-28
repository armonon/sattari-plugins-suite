#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"
#include <vector>

class SattariPulseAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SattariPulseAudioProcessorEditor (SattariPulseAudioProcessor&);
    ~SattariPulseAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void addKnob (const juce::String& id, const juce::String& cap, const juce::String& suffix = {});
    void addCombo (juce::ComboBox& box, const juce::String& id, const juce::StringArray& items,
                   juce::Label& cap, const juce::String& capText);

    SattariPulseAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("pulse") };

    juce::Label title, subtitle, tempoLabel, shapeCap, rateCap;
    juce::ComboBox shape, rate;
    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::Label>  caps;
    std::vector<std::unique_ptr<SA>> atts;
    std::unique_ptr<CA> shapeAtt, rateAtt;

    // Live pump indicator (curve gain 0..1) driven by the timer.
    float meterGain = 1.0f;
    juce::Rectangle<int> meterBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariPulseAudioProcessorEditor)
};
