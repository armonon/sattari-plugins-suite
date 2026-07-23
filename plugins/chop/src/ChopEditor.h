#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariBranding.h>
#include "ChopProcessor.h"

// Sattari Chop editor. v0: load a file, see the waveform + slice markers, drag
// Sensitivity to re-slice live, play the slices from MIDI (C1 upward).
// The full §3 direct-manipulation grammar (click to add, drag to move, drag off to
// delete, snap magnetism, zoom, nudge) lands on top of this.
class SattariChopAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              public juce::FileDragAndDropTarget,
                                              private juce::Timer
{
public:
    explicit SattariChopAudioProcessorEditor (SattariChopAudioProcessor&);
    ~SattariChopAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void openFileChooser();
    void rebuildPeaks();
    void drawWaveform (juce::Graphics&, juce::Rectangle<int> area);

    SattariChopAudioProcessor& processor;

    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("chop") };

    juce::Label title, subtitle, statusLabel;
    juce::TextButton loadButton { "Load audio..." };
    juce::Slider sensitivitySlider, outputSlider;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAtts;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Rectangle<int> waveformArea;

    // Downsampled min/max peaks for drawing (rebuilt when the source changes).
    std::vector<float> peakMin, peakMax;
    int peaksForWidth = 0;
    const void* peaksForSource = nullptr;

    float lastSensitivity = -1.0f;
    int lastSliceCount = -1;
    bool dragHighlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariChopAudioProcessorEditor)
};
