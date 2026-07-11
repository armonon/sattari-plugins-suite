#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include "BrainProcessor.h"

// =============================================================================
//  Sattari Brain — editor
// -----------------------------------------------------------------------------
//  A clear, sectioned panel in the shared SattariLookAndFeel:
//    * a big detected-key / confidence / reference / tempo readout,
//    * a manual key override,
//    * a reference-tuning override,
//    * the editable SessionState controls (chord, time sig, swing, section,
//      energy, maqam) and the Publish master switch.
//  Functional over fancy — every control is directly the thing it edits.
// =============================================================================
class SattariBrainAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SattariBrainAudioProcessorEditor (SattariBrainAudioProcessor&);
    ~SattariBrainAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void setupCombo (juce::ComboBox&, const juce::StringArray&);
    void setupSlider (juce::Slider&, const juce::String& suffix = {});
    static void styleCaption (juce::Label&, const juce::String&);

    SattariBrainAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("brain") };

    juce::Label title, subtitle;

    // Master + detection controls.
    juce::ToggleButton publishToggle { "Publish to Suite" };
    juce::TextButton   rescanButton   { "Re-scan" };
    juce::TextButton   midiLearnButton { "MIDI Learn" };

    // Manual key override.
    juce::ToggleButton manualToggle { "Manual Key" };
    juce::ComboBox     manualRootBox, manualScaleBox;

    // Reference tuning override.
    juce::ToggleButton refOverrideToggle { "Reference" };
    juce::Slider       referenceSlider;
    juce::Label        capRef;

    // Editable SessionState.
    juce::ComboBox chordRootBox, chordQualityBox, sectionBox, timeSigDenBox;
    juce::Slider   timeSigNumSlider, swingSlider, energySlider, maqamSlider;
    juce::Label    capChordRoot, capChordQ, capSection, capTsNum, capTsDen, capSwing, capEnergy, capMaqam;

    std::unique_ptr<ButtonAttachment> publishAtt, manualAtt, refOverrideAtt;
    std::unique_ptr<ComboAttachment>  manualRootAtt, manualScaleAtt, chordRootAtt, chordQualityAtt, sectionAtt, timeSigDenAtt;
    std::unique_ptr<SliderAttachment> referenceAtt, timeSigNumAtt, swingAtt, energyAtt, maqamAtt;

    SattariBrainAudioProcessor::Detected snapshot;
    juce::Rectangle<int> keyArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariBrainAudioProcessorEditor)
};
