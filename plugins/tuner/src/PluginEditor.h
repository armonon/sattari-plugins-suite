#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAnalysis.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariSessionHud.h>
#include "PluginProcessor.h"

class SattariTunerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SattariTunerAudioProcessorEditor (SattariTunerAudioProcessor&);
    ~SattariTunerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void updateReading();
    void paintTunerFace (juce::Graphics&, juce::Rectangle<int>);

    SattariTunerAudioProcessor& audioProcessor;
    const juce::Colour accent {
        sattari::brand::productAccent ("tuner")
    };
    sattari::studio::LookAndFeel lookAndFeel {
        sattari::brand::productAccent ("tuner")
    };
    sattari::studio::Header header {
        "Sattari Tuner", "PRECISION PITCH ANALYSIS", accent
    };

    juce::ComboBox factoryPreset;
    sattari::ui::SessionHud sessionHud;
    sattari::studio::MacroControl referenceA { "Reference A", " Hz" };
    sattari::studio::MacroControl smoothing { "Smoothing" };
    sattari::studio::MacroControl transpose { "Transpose", " st" };
    std::unique_ptr<SliderAttachment> referenceAttachment;
    std::unique_ptr<SliderAttachment> smoothingAttachment;
    std::unique_ptr<SliderAttachment> transposeAttachment;

    SattariTunerAudioProcessor::LiveReading reading;
    float needleCents = 0.0f;
    float tuneGlow = 0.0f;
    juce::Rectangle<int> tunerArea;
    juce::Rectangle<int> controlsArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (
        SattariTunerAudioProcessorEditor)
};
