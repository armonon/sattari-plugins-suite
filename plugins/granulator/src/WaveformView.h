#pragma once

#include <JuceHeader.h>

#include <sattari_audio_core/SattariGranularEngine.h>

#include "PluginProcessor.h"

#include <memory>

// =============================================================================
//  Sattari Granulator — the always-visible source view
// -----------------------------------------------------------------------------
//  A juce::Component + FileDragAndDropTarget that renders the current
//  SourceSample and lets the player shape it:
//
//    * an IN-MEMORY juce::AudioThumbnail (reset + addBlock) so captured/edited
//      (non-file) buffers still render — the thumbnail is rebuilt whenever the
//      processor's source version changes;
//    * draggable Start / End handles (~8px hit-zones) that write srcStart /
//      srcEnd via setValueNotifyingHost (cheap automatable offsets, no rebuild);
//    * an accent-filled region between Start/End, dimmed outside, with a loop
//      sub-span bracket when Loop is engaged;
//    * a live File-Position cursor + short grain "spark" markers, read from the
//      processor's lock-free GrainVizFrame on a ~30 Hz timer (no audio-thread
//      UI access);
//    * coarse transient tick marks along the top rail (a debounced onset scan
//      over the decoded buffer, recomputed only when the source changes);
//    * audio-file drag-drop (wav/aiff/flac/mp3/ogg) -> processor.loadFile().
//
//  SWAP SAFETY: the view never mutates a buffer. Every edit routes through the
//  processor, which publishes a fresh immutable SourceSample via atomic_store;
//  the old buffer's last reference drops on the message thread.
// =============================================================================
class WaveformView final : public juce::Component,
                           public juce::FileDragAndDropTarget,
                           private juce::Timer
{
public:
    explicit WaveformView (SattariGranulatorAudioProcessor& processor);
    ~WaveformView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Drag-drop -----------------------------------------------------------------
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit  (const juce::StringArray& files) override;
    void filesDropped  (const juce::StringArray& files, int x, int y) override;

    // Start/End handle dragging -------------------------------------------------
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    // Rebuild the in-memory thumbnail + transient rail from the current source.
    void refreshFromSource();

    // Geometry: the inner rectangle the waveform + handles live in.
    juce::Rectangle<float> waveArea() const;

    // Map a normalised position 0..1 <-> an x coordinate inside waveArea().
    float xForPos (float pos01) const;
    float posForX (float x) const;

    // Cheap param helpers.
    float paramValue (const char* id) const;
    void  setParamNormalising (const char* id, float value);

    // Which draggable element (if any) is under a point.
    enum class Handle { None, Start, End };
    Handle handleAt (juce::Point<float> p) const;

    SattariGranulatorAudioProcessor& audioProcessor;

    juce::AudioThumbnailCache thumbnailCache { 4 };
    juce::AudioThumbnail      thumbnail;

    // Source-version bookkeeping so we only rebuild the thumbnail on real change.
    juce::uint32 lastSourceVersion = 0xFFFFFFFFu;
    bool         haveSource        = false;
    double       thumbLengthSecs   = 0.0;

    // Coarse onset marks (normalised 0..1), recomputed on source change only.
    std::vector<float> transientMarks;

    // Live viz snapshot (updated by the 30 Hz timer).
    sattari::dsp::SattariGranularEngine::GrainVizFrame viz;

    // Drag state.
    Handle activeHandle = Handle::None;
    Handle hoverHandle  = Handle::None;
    bool   fileHovering = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
