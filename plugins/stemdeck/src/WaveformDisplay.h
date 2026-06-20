#pragma once

#include <JuceHeader.h>

class WaveformDisplay final : public juce::Component,
                              public juce::FileDragAndDropTarget,
                              private juce::ChangeListener
{
public:
    WaveformDisplay (juce::AudioFormatManager& manager, juce::AudioThumbnailCache& cache, juce::String deckName);
    ~WaveformDisplay() override;

    void setFile (const juce::File& file);
    void setOnFileDropped (std::function<void(const juce::File&)> callback);
    void paint (juce::Graphics& g) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::AudioThumbnail thumbnail;
    juce::String title;
    juce::File currentFile;
    std::function<void(const juce::File&)> onFileDropped;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformDisplay)
};
