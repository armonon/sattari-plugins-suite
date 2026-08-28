#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"
#include <array>

class SattariImageAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SattariImageAudioProcessorEditor (SattariImageAudioProcessor&);
    ~SattariImageAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void styleKnob (juce::Slider&, const juce::String& suffix = {});
    void styleCaption (juce::Label&, const juce::String&);

    SattariImageAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("imager") };

    juce::Label title, subtitle;

    static constexpr int kKnobs = 7;   // width, low, mid, high, loMid, midHi, haas
    std::array<juce::Slider, kKnobs> knobs;
    std::array<juce::Label,  kKnobs> caps;
    std::array<std::unique_ptr<SliderAttachment>, kKnobs> knobAtts;

    juce::Slider lowMonoFreq;
    juce::Label  lowMonoFreqCap;
    std::unique_ptr<SliderAttachment> lowMonoFreqAtt;

    juce::ToggleButton lowMonoBtn { "Low Mono" };
    juce::ToggleButton monoAuditionBtn { "Mono Audition" };
    std::unique_ptr<ButtonAttachment> lowMonoAtt, monoAuditionAtt;

    float shownCorr = 0.0f;   // smoothed for the readout

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariImageAudioProcessorEditor)
};
