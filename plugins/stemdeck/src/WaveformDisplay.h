#pragma once

#include <JuceHeader.h>

class WaveformDisplay final : public juce::Component,
                              public juce::FileDragAndDropTarget,
                              private juce::ChangeListener
{
public:
    static constexpr int numHotCues = 8;

    struct HotCue
    {
        double   seconds = -1.0;    // < 0 means unset
        juce::Colour colour;
    };

    WaveformDisplay (juce::AudioFormatManager& manager, juce::AudioThumbnailCache& cache, juce::String deckName);
    ~WaveformDisplay() override;

    void setFile (const juce::File& file);
    void setStemFile (int laneIndex, const juce::File& file);   // 1=drums 2=bass 3=music 4=vocals
    void setOnFileDropped (std::function<void(const juce::File&)> callback);
    void setOnSeekRequest (std::function<void(double /*seconds*/)> callback);
    void setPlaybackState (double positionSeconds, double totalSeconds, bool isPlaying);

    // Hot cues — drawn as coloured tick marks on the waveform
    void setHotCues (const std::array<HotCue, numHotCues>& cues);

    // Loop region — drawn as a tinted band
    void setLoopRegion (double inSeconds, double outSeconds, bool active);
    void clearLoopRegion();

    // Beat grid — draw subtle tick marks for beat positions
    void setBeatGridSeconds (std::vector<double> beatSeconds);

    // Zoom: level=1 = full track, level=4 = 4× zoomed in; scroll 0..1
    void setZoom (float level, float scrollFraction);

    // Intro end / Outro start DJ markers (< 0 = unset)
    void setIntroEnd   (double seconds) noexcept { introEndSeconds   = seconds; repaint(); }
    void setOutroStart (double seconds) noexcept { outroStartSeconds = seconds; repaint(); }

    // Show RTSS badge when real-time separation is active with no offline stems
    void setRtssActive (bool active);
    bool getRtssActive() const noexcept { return rtssActive; }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // Returns the [startTime, endTime] seconds window based on current zoom/scroll
    std::pair<double, double> visibleTimeRange (double totalLength) const;

    juce::AudioThumbnail thumbnail;
    juce::AudioThumbnail drumsThumbnail;
    juce::AudioThumbnail bassThumbnail;
    juce::AudioThumbnail musicThumbnail;
    juce::AudioThumbnail vocalsThumbnail;

    juce::String title;
    juce::File currentFile;
    std::function<void(const juce::File&)> onFileDropped;
    std::function<void(double)> onSeekRequest;

    double playbackPositionSeconds = 0.0;
    double playbackTotalSeconds    = 0.0;
    bool   isPlaying               = false;

    std::array<HotCue, numHotCues> hotCues {};
    double loopInSeconds  = -1.0;
    double loopOutSeconds = -1.0;
    bool   loopActive     = false;

    std::vector<double> beatGridSeconds;
    double introEndSeconds   = -1.0;
    double outroStartSeconds = -1.0;

    float zoomLevel      = 1.0f;
    float scrollFraction = 0.0f;
    bool  rtssActive     = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformDisplay)
};
