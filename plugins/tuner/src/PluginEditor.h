#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
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
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void styleKnob (juce::Slider&);
    void styleCaption (juce::Label&, const juce::String&);
    void paintTunerFace (juce::Graphics&, juce::Rectangle<float> area);

    SattariTunerAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("tuner") };

    juce::Label title, subtitle;

    juce::Slider referenceA, smoothing, transpose;
    juce::Label  referenceACap, smoothingCap, transposeCap;
    std::unique_ptr<SliderAttachment> referenceAAtt, smoothingAtt, transposeAtt;

    // Smoothed needle position (cents) for a fluid meter, plus in-tune glow.
    float needleCents = 0.0f;
    float tuneGlow    = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariTunerAudioProcessorEditor)
};
