#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariStudio.h>
#include "BrainProcessor.h"

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
    void setupCombo (juce::ComboBox&, const juce::StringArray&, const juce::String&);
    void setupSlider (juce::Slider&, const juce::String&, const juce::String& suffix = {});
    void setupCaption (juce::Label&, const juce::String&);
    void paintSessionHero (juce::Graphics&);

    SattariBrainAudioProcessor& audioProcessor;
    const juce::Colour accent {
        sattari::brand::productAccent ("brain")
    };
    sattari::studio::LookAndFeel lookAndFeel {
        sattari::brand::productAccent ("brain")
    };
    sattari::studio::Header header {
        "Sattari Brain", "SESSION INTELLIGENCE", accent
    };

    juce::ToggleButton publishToggle { "Publish to suite" };
    juce::TextButton rescanButton { "Analyse now" };
    juce::TextButton midiLearnButton { "MIDI learn" };

    juce::ToggleButton manualToggle { "Manual key" };
    juce::ComboBox manualRootBox, manualScaleBox;
    juce::ToggleButton refOverrideToggle { "Reference override" };
    juce::Slider referenceSlider;
    juce::Label capManualRoot, capManualScale, capRef;

    juce::ComboBox chordRootBox, chordQualityBox, sectionBox, timeSigDenBox;
    juce::Slider timeSigNumSlider, swingSlider, energySlider, maqamSlider;
    juce::Label capChordRoot, capChordQ, capSection, capTsNum, capTsDen,
                capSwing, capEnergy, capMaqam;

    sattari::studio::MetricCard tempoMetric { "Tempo" };
    sattari::studio::MetricCard tuningMetric { "Reference" };
    sattari::studio::MetricCard meterMetric { "Meter" };

    std::unique_ptr<ButtonAttachment> publishAtt, manualAtt, refOverrideAtt;
    std::unique_ptr<ComboAttachment> manualRootAtt, manualScaleAtt,
                                     chordRootAtt, chordQualityAtt,
                                     sectionAtt, timeSigDenAtt;
    std::unique_ptr<SliderAttachment> referenceAtt, timeSigNumAtt,
                                      swingAtt, energyAtt, maqamAtt;

    SattariBrainAudioProcessor::Detected snapshot;
    juce::Rectangle<int> heroArea, tuningArea, contextArea, confidenceArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariBrainAudioProcessorEditor)
};
