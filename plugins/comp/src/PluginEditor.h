#pragma once

#include <JuceHeader.h>
#include <array>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"

class SattariCompAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit SattariCompAudioProcessorEditor (SattariCompAudioProcessor&);
    ~SattariCompAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void setupSlider (juce::Slider&, juce::Label&, const juce::String& text, const juce::String& suffix);
    void timerCallback() override;

    SattariCompAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("comp") };

    juce::Label title, subtitle, grLabel, releaseInfo;

    // 8 rotary knobs.
    juce::Slider threshold, ratio, knee, attack, release, makeup, mix, scHpf;
    juce::Label  thresholdCap, ratioCap, kneeCap, attackCap, releaseCap, makeupCap, mixCap, scHpfCap;
    std::array<std::unique_ptr<SliderAttachment>, 8> knobAtts;

    // Toggles + tempo-release rate.
    juce::ToggleButton autoRelease, autoGain, scListen, tempoRelease;
    juce::ComboBox releaseDiv;
    juce::Label releaseDivCap;
    std::unique_ptr<ButtonAttachment> autoRelAtt, autoGainAtt, scListenAtt, tempoRelAtt;
    std::unique_ptr<ComboAttachment>  releaseDivAtt;

    // Rolling gain-reduction history for the meter graph.
    static constexpr int grHistorySize = 120;
    std::array<float, grHistorySize> grHistory {};
    int grHistoryPos = 0;
    juce::Rectangle<int> grGraphBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariCompAudioProcessorEditor)
};
