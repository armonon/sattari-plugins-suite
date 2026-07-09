#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
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
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void drawSpectrum (juce::Graphics&, juce::Rectangle<int>, juce::Colour accent);
    void drawGonio    (juce::Graphics&, juce::Rectangle<int>, juce::Colour accent);
    void drawCorrelation (juce::Graphics&, juce::Rectangle<int>, juce::Colour accent);

    SattariScopeAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("scope") };

    juce::Label title, subtitle, tiltCap;
    juce::Slider tilt;
    juce::ToggleButton chromaToggle { "Pitch-Class Overlay" };
    std::unique_ptr<SliderAttachment> tiltAtt;
    std::unique_ptr<ButtonAttachment> chromaAtt;

    SattariScopeAudioProcessor::Spectrum spec;
    SattariScopeAudioProcessor::Vector   vec;

    static const char* noteName (int pc) noexcept
    {
        static const char* names[12] =
            { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return (pc >= 0 && pc < 12) ? names[pc] : "--";
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariScopeAudioProcessorEditor)
};
