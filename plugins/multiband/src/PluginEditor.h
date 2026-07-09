#pragma once

#include <JuceHeader.h>
#include <array>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"

class SattariMultibandAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                   private juce::Timer
{
public:
    explicit SattariMultibandAudioProcessorEditor (SattariMultibandAudioProcessor&);
    ~SattariMultibandAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    static constexpr int kBands = SattariMultibandAudioProcessor::kBands;
    static constexpr int kPerBand = 5;   // thr, ratio, atk, rel, mk

    void styleKnob (juce::Slider&, const juce::String& suffix);
    void styleCaption (juce::Label&, const juce::String&);
    void timerCallback() override;

    SattariMultibandAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("multiband") };

    juce::Label title, subtitle;

    // Per-band controls: 5 knobs + a solo toggle + a band header label + GR meter.
    struct BandUI
    {
        juce::Label header;
        std::array<juce::Slider, kPerBand> knobs;
        std::array<juce::Label, kPerBand>  caps;
        std::array<std::unique_ptr<SliderAttachment>, kPerBand> atts;
        juce::ToggleButton solo;
        std::unique_ptr<ButtonAttachment> soloAtt;
        juce::Rectangle<int> meterBounds;
        float grSmoothed = 0.0f;
    };
    std::array<BandUI, kBands> bands;

    // Crossovers + global controls.
    juce::Slider xLow, xHigh, mix, output;
    juce::Label  xLowCap, xHighCap, mixCap, outputCap;
    std::array<std::unique_ptr<SliderAttachment>, 4> globalAtts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariMultibandAudioProcessorEditor)
};
