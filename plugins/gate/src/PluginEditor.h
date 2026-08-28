#pragma once

#include <JuceHeader.h>
#include <array>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "PluginProcessor.h"

class SattariGateAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit SattariGateAudioProcessorEditor (SattariGateAudioProcessor&);
    ~SattariGateAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void setupSlider (juce::Slider&, juce::Label&, const juce::String& text, const juce::String& suffix);
    void timerCallback() override;

    SattariGateAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("gate") };

    juce::Label title, subtitle, grLabel, stateLabel;

    // 10 rotary knobs.
    static constexpr int numKnobs = 10;
    juce::Slider threshold, range, ratio, attack, hold, release, lookahead, hysteresis, scHpf, scLpf;
    juce::Label  thresholdCap, rangeCap, ratioCap, attackCap, holdCap, releaseCap,
                 lookaheadCap, hysteresisCap, scHpfCap, scLpfCap;
    std::array<std::unique_ptr<SliderAttachment>, (size_t) numKnobs> knobAtts;

    // Toggles + detector + tempo-gate.
    juce::ToggleButton scListen, expander, tempoGate;
    juce::ComboBox detMode, tempoDiv;
    juce::Label detModeCap, tempoDivCap;
    juce::Slider tempoDuty;
    juce::Label tempoDutyCap;
    std::unique_ptr<ButtonAttachment> scListenAtt, expanderAtt, tempoGateAtt;
    std::unique_ptr<ComboAttachment>  detModeAtt, tempoDivAtt;
    std::unique_ptr<SliderAttachment> tempoDutyAtt;

    // Rolling gain-reduction history for the meter graph.
    static constexpr int grHistorySize = 120;
    std::array<float, grHistorySize> grHistory {};
    int grHistoryPos = 0;
    juce::Rectangle<int> grGraphBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariGateAudioProcessorEditor)
};
