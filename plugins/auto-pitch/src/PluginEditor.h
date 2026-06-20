#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "PluginProcessor.h"

class SattariAutoPitchLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SattariAutoPitchLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
};

class PitchCompassStub final : public juce::Component
{
public:
    PitchCompassStub() = default;
    void paint (juce::Graphics&) override;
};

class SattariAutoPitchAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SattariAutoPitchAudioProcessorEditor (SattariAutoPitchAudioProcessor&);
    ~SattariAutoPitchAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void setupSlider (juce::Slider&, const juce::String& suffix = {});
    void setupCombo (juce::ComboBox&);
    void setupLabel (juce::Label&, const juce::String& text, float size = 14.0f);
    void setupCaption (juce::Label&, const juce::String& text);
    void loadTraceFromClipboard();
    void refreshUserPresets();
    void saveCurrentAsUserPreset();
    void drawCard (juce::Graphics&, juce::Rectangle<float>, const juce::String& eyebrow,
                   const juce::String& titleText, juce::Colour accent) const;

    SattariAutoPitchAudioProcessor& audioProcessor;
    SattariAutoPitchLookAndFeel lookAndFeel;

    // Shared cross-plugin preset system (styled by this plugin's own look).
    sattari::presets::SattariPresetManager presetManager;
    juce::ComboBox userPresets;
    juce::TextButton savePresetButton { "Save Preset" };

    juce::Label title, subtitle, keyLabel, correctionLabel, gainLabel, traceLabel, traceStatus, keyDetectionStatus;
    juce::Label amountCaption, speedCaption, confidenceCaption, inputCaption, outputCaption, formantCaption;
    juce::ComboBox keyRoot, scale, style, factoryPresets;
    juce::Slider amount, speed, inputGain, outputGain, traceConfidence, formantShift;
    juce::ToggleButton doNoHarm, enableCorrection, autoKey, formantPreserve;
    juce::TextButton loadTraceButton { "Load trace JSON" };
    PitchCompassStub pitchCompass;

    juce::Rectangle<int> headerArea, keyCard, tuneCard, utilityCard, compassCard, statusCard;

    void registerFactoryPresets();

    std::unique_ptr<ComboAttachment> keyRootAttach, scaleAttach, styleAttach;
    std::unique_ptr<SliderAttachment> amountAttach, speedAttach, inputGainAttach, outputGainAttach, traceConfidenceAttach, formantShiftAttach;
    std::unique_ptr<ButtonAttachment> doNoHarmAttach, enableCorrectionAttach, autoKeyAttach, formantPreserveAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariAutoPitchAudioProcessorEditor)
};
