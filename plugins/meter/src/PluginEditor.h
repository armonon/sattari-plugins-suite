#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAnalysis.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariSessionHud.h>
#include "PluginProcessor.h"

class SattariMeterAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SattariMeterAudioProcessorEditor (SattariMeterAudioProcessor&);
    ~SattariMeterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void updateReadout();

    SattariMeterAudioProcessor& audioProcessor;
    const juce::Colour accent {
        sattari::brand::productAccent ("meter")
    };
    sattari::studio::LookAndFeel lookAndFeel {
        sattari::brand::productAccent ("meter")
    };
    sattari::studio::Header header {
        "Sattari Meter", "LOUDNESS / TRUE-PEAK ANALYSIS", accent
    };

    sattari::studio::analysis::Metric momentary { "Momentary", accent };
    sattari::studio::analysis::Metric shortTerm { "Short-term", accent };
    sattari::studio::analysis::Metric integrated { "Integrated", accent };
    sattari::studio::analysis::Metric truePeak { "True peak", accent };

    sattari::ui::SessionHud sessionHud;
    juce::Label targetLabel;
    juce::Slider target;
    juce::TextButton resetButton { "Reset integrated" };
    std::unique_ptr<SliderAttachment> targetAttachment;

    SattariMeterAudioProcessor::Readout readout;
    juce::Rectangle<int> correlationArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (
        SattariMeterAudioProcessorEditor)
};
