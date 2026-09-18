#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <memory>
#include <atomic>
#include <functional>

namespace sattari::arrangement
{
#if JucePlugin_Enable_ARA
struct ClipValidity { std::atomic<bool> current { true }; };

// A separate document-thread listener keeps completed results honest too. The
// reader's own invalidation protects reads, but cannot revoke an already-published
// key. Its shared flag also closes the read-complete/analysis-still-running gap.
class ClipWatch final : private juce::ARAPlaybackRegion::Listener,
                        private juce::ARAAudioSource::Listener
{
public:
    explicit ClipWatch (juce::ARAPlaybackRegion* regionIn) : region (regionIn),
        source (region->getAudioModification()->getAudioSource())
    {
        region->addListener (this);
        source->addListener (this);
    }
    ~ClipWatch() override
    {
        if (region) region->removeListener (this);
        if (source) source->removeListener (this);
    }
    std::shared_ptr<ClipValidity> validity = std::make_shared<ClipValidity>();
    std::function<void()> onInvalidated;
private:
    void invalidate()
    {
        if (! validity->current.exchange (false)) return;
        if (onInvalidated) onInvalidated();
    }
    void willUpdatePlaybackRegionProperties (juce::ARAPlaybackRegion* r, juce::ARAPlaybackRegion::PropertiesPtr p) override
    {
        if (! juce::exactlyEqual (r->getStartInPlaybackTime(), p->startInPlaybackTime)
            || ! juce::exactlyEqual (r->getDurationInPlaybackTime(), p->durationInPlaybackTime)
            || ! juce::exactlyEqual (r->getStartInAudioModificationTime(), p->startInModificationTime)
            || ! juce::exactlyEqual (r->getDurationInAudioModificationTime(), p->durationInModificationTime)
            || r->isTimestretchEnabled() != ((p->transformationFlags & ARA::kARAPlaybackTransformationTimestretch) != 0)
            || r->isTimeStretchReflectingTempo() != ((p->transformationFlags & ARA::kARAPlaybackTransformationTimestretchReflectingTempo) != 0)
            || r->hasContentBasedFadeAtHead() != ((p->transformationFlags & ARA::kARAPlaybackTransformationContentBasedFadeAtHead) != 0)
            || r->hasContentBasedFadeAtTail() != ((p->transformationFlags & ARA::kARAPlaybackTransformationContentBasedFadeAtTail) != 0)) invalidate();
    }
    void didUpdatePlaybackRegionContent (juce::ARAPlaybackRegion*, juce::ARAContentUpdateScopes flags) override
    { if (flags.affectSamples()) invalidate(); }
    void willDestroyPlaybackRegion (juce::ARAPlaybackRegion* r) override
    { r->removeListener (this); region = nullptr; invalidate(); }
    void willUpdateAudioSourceProperties (juce::ARAAudioSource* s, juce::ARAAudioSource::PropertiesPtr p) override
    {
        if (! juce::exactlyEqual (s->getSampleRate(), p->sampleRate) || s->getSampleCount() != p->sampleCount
            || s->getChannelCount() != p->channelCount) invalidate();
    }
    void doUpdateAudioSourceContent (juce::ARAAudioSource*, juce::ARAContentUpdateScopes flags) override
    { if (flags.affectSamples()) invalidate(); }
    void willDestroyAudioSource (juce::ARAAudioSource* s) override
    { s->removeListener (this); source = nullptr; invalidate(); }
    juce::ARAPlaybackRegion* region;
    juce::ARAAudioSource* source;
};

class TrackedRegionReader final : public juce::ARAPlaybackRegionReader
{
public:
    TrackedRegionReader (juce::ARAPlaybackRegion* region, std::shared_ptr<ClipValidity> state)
        : ARAPlaybackRegionReader (region), validity (std::move (state)) {}
    bool current() const { return validity->current.load(); }
private:
    std::shared_ptr<ClipValidity> validity;
};
#endif

inline bool isCurrent (juce::AudioFormatReader* reader)
{
#if JucePlugin_Enable_ARA
    if (auto* tracked = dynamic_cast<TrackedRegionReader*> (reader)) return tracked->current();
#else
    juce::ignoreUnused (reader);
#endif
    return true;
}

struct ClipReadRequest
{
    std::unique_ptr<juce::AudioFormatReader> reader;
#if JucePlugin_Enable_ARA
    std::unique_ptr<ClipWatch> watch;
#endif
    juce::String name;
    double timelineStartSeconds = 0.0;
    juce::String error;
};

inline juce::String unavailableMessage()
{
    return "Direct clip scanning requires an ARA-capable DAW and this plugin loaded as an ARA extension. "
           "A normal AU/VST3 insert cannot read the arrangement selection. "
           "Use Listen live or choose/drop audio in hosts without ARA.";
}

#if JucePlugin_Enable_ARA
// Call only from the open editor on the document/message thread. Keep the
// returned reader owned there until the analysis worker has stopped. JUCE's
// region reader invalidates itself if the host changes/deletes the region;
// read failures must abort the analysis, never publish a partial verdict.
inline ClipReadRequest selectedClip (juce::ARAEditorView* view)
{
    ClipReadRequest request;
    if (view == nullptr)
    {
        request.error = unavailableMessage();
        return request;
    }
    const auto regions = view->getViewSelection().getEffectivePlaybackRegions<juce::ARAPlaybackRegion>();
    if (regions.size() != 1)
    {
        request.error = regions.empty()
            ? "Select one audio clip linked to this ARA instance in the DAW, then click Scan DAW clip."
            : "Select a single audio clip to analyse. Multiple selected clips are not combined into one key guess.";
        return request;
    }
    auto* region = regions.front();
    auto* source = region->getAudioModification()->getAudioSource();
    const double sourceDuration = region->getDurationInAudioModificationTime();
    const double playbackDuration = region->getDurationInPlaybackTime();
    if (! source->isSampleAccessEnabled())
    {
        request.error = "The DAW has not enabled audio access for this clip yet. Try again when it is available.";
        return request;
    }
    // The shared raw renderer is not a warp engine. Reject these regions rather
    // than analysing different notes/timing from the selected arrangement clip.
    if (region->isTimestretchEnabled() || region->isTimeStretchReflectingTempo()
        || ! std::isfinite (region->getStartInPlaybackTime())
        || ! std::isfinite (source->getSampleRate())
        || ! std::isfinite (sourceDuration) || ! std::isfinite (playbackDuration)
        || sourceDuration <= 0.0 || playbackDuration <= 0.0 || source->getSampleRate() <= 0.0
        || std::abs (sourceDuration - playbackDuration) > 1.0 / source->getSampleRate())
    {
        request.error = "Direct scanning currently supports unwarped audio clips. "
                        "For a time-stretched clip, use Listen live to analyse its rendered playback.";
        return request;
    }
    request.name = region->getName() ? juce::String::fromUTF8 (region->getName()) : juce::String();
    if (request.name.isEmpty()) request.name = "Selected DAW clip";
    request.timelineStartSeconds = region->getStartInPlaybackTime();
    request.watch = std::make_unique<ClipWatch> (region);
    request.reader = std::make_unique<TrackedRegionReader> (region, request.watch->validity);
    if (request.reader->lengthInSamples <= 0)
    {
        request.reader.reset();
        request.error = "The selected clip contains no readable audio.";
    }
    return request;
}
#endif
}
