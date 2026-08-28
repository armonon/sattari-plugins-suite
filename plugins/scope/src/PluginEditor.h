#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAnalysis.h>
#include <sattari_audio_core/SattariBranding.h>
#include "PluginProcessor.h"

class SattariScopeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SattariScopeAudioProcessorEditor (SattariScopeAudioProcessor&);
    ~SattariScopeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment =
        juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void updateSnapshots();
    void drawSpectrum (juce::Graphics&, juce::Rectangle<int>);
    void drawGoniometer (juce::Graphics&, juce::Rectangle<int>);
    void drawCorrelation (juce::Graphics&, juce::Rectangle<int>);

    static const char* noteName (int pc) noexcept
    {
        static const char* names[12] = {
            "C", "C#", "D", "D#", "E", "F",
            "F#", "G", "G#", "A", "A#", "B"
        };
        return (pc >= 0 && pc < 12) ? names[pc] : "--";
    }

    SattariScopeAudioProcessor& audioProcessor;
    const juce::Colour accent {
        sattari::brand::productAccent ("scope")
    };
    sattari::studio::LookAndFeel lookAndFeel {
        sattari::brand::productAccent ("scope")
    };
    sattari::studio::Header header {
        "Sattari Scope", "SPECTRUM / STEREO FIELD ANALYSIS", accent
    };

    juce::Label tiltLabel;
    juce::Slider tilt;
    juce::ToggleButton chromaToggle { "Pitch-class overlay" };
    std::unique_ptr<SliderAttachment> tiltAttachment;
    std::unique_ptr<ButtonAttachment> chromaAttachment;

    sattari::studio::analysis::Metric keyMetric { "Session key", accent };
    SattariScopeAudioProcessor::Spectrum spectrum;
    SattariScopeAudioProcessor::Vector vector;

    juce::Rectangle<int> spectrumArea;
    juce::Rectangle<int> goniometerArea;
    juce::Rectangle<int> correlationArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (
        SattariScopeAudioProcessorEditor)
};
