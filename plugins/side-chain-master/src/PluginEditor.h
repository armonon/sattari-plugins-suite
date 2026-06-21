#pragma once

#include <JuceHeader.h>
#include <array>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "PluginProcessor.h"

class SideChainMasterAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                  private juce::Timer
{
public:
    explicit SideChainMasterAudioProcessorEditor (SideChainMasterAudioProcessor&);
    ~SideChainMasterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void setupSlider (juce::Slider&, juce::Label&, const juce::String& text);
    void setupCombo (juce::ComboBox&, juce::Label&, const juce::String& text);
    void timerCallback() override;
    void refreshUserPresets();
    void saveCurrentAsUserPreset();
    void registerFactoryPresets();

    SideChainMasterAudioProcessor& audioProcessor;

    // Shared suite UI + cross-plugin preset system.
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("side-chain-master") };
    sattari::presets::SattariPresetManager presetManager;
    juce::ComboBox factoryPresets, userPresets;
    juce::TextButton savePresetButton { "Save Preset" };

    juce::Label title, subtitle, grLabel, sidechainStatus;
    juce::ComboBox mode, syncRate;
    juce::Slider threshold, ratio, attack, release, depth, makeup, mix, sidechainHpf, lookahead, syncCurve;
    juce::ToggleButton external, listen, syncDuck;

    juce::Label modeLabel, thresholdLabel, ratioLabel, attackLabel, releaseLabel, depthLabel, makeupLabel, mixLabel, hpfLabel,
                lookaheadLabel, syncCurveLabel, syncRateLabel;

    std::unique_ptr<ComboAttachment> modeAttach, syncRateAttach;
    std::unique_ptr<SliderAttachment> thresholdAttach, ratioAttach, attackAttach, releaseAttach, depthAttach, makeupAttach, mixAttach, hpfAttach, lookaheadAttach, syncCurveAttach;
    std::unique_ptr<ButtonAttachment> externalAttach, listenAttach, syncDuckAttach;

    // Rolling gain-reduction history for the GR graph (filled by the timer).
    static constexpr int grHistorySize = 120;
    std::array<float, grHistorySize> grHistory {};
    int grHistoryPos = 0;
    juce::Rectangle<int> grGraphBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SideChainMasterAudioProcessorEditor)
};
