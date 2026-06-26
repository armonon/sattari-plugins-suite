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
    void setStemFile (int laneIndex, const juce::File& file);   // laneIndex: 1=drums 2=bass 3=music 4=vocals
    void setOnFileDropped (std::function<void(const juce::File&)> callback);
    void setPlaybackState (double positionSeconds, double totalSeconds, bool isPlaying);
    void paint (juce::Graphics& g) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::AudioThumbnail thumbnail;
    juce::AudioThumbnail drumsThumbnail;
    juce::AudioThumbnail bassThumbnail;
    juce::AudioThumbnail musicThumbnail;
    juce::AudioThumbnail vocalsThumbnail;

    juce::String title;
    juce::File currentFile;
    std::function<void(const juce::File&)> onFileDropped;
    double playbackPositionSeconds = 0.0;
    double playbackTotalSeconds = 0.0;
    bool   isPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformDisplay)
};
