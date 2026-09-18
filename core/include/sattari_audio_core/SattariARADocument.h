#pragma once

// Shared raw ARA region playback for Key and Auto Pitch. No tuning DSP runs here.
#include <JuceHeader.h>

#if JucePlugin_Enable_ARA

#include <map>
#include <set>
#include <sattari_audio_core/SattariClipPlayback.h>

namespace
{
class SattariARAPlaybackRenderer;
struct SattariARAModelState
{
    juce::ReadWriteLock lock;
    std::vector<SattariARAPlaybackRenderer*> renderers;
};
class SattariARAReadThread final : public juce::TimeSliceThread
{
public:
    SattariARAReadThread() : juce::TimeSliceThread ("Sattari ARA Reader")
    {
        startThread (juce::Thread::Priority::high);
    }
};

class SattariARAPlaybackRenderer final : public juce::ARAPlaybackRenderer
{
public:
    SattariARAPlaybackRenderer (ARA::PlugIn::DocumentController* controller,
                                  std::shared_ptr<SattariARAModelState> stateIn)
        : juce::ARAPlaybackRenderer (controller), state (std::move (stateIn))
    {
        const juce::ScopedWriteLock guard (state->lock);
        state->renderers.push_back (this);
    }

    ~SattariARAPlaybackRenderer() override
    {
        const juce::ScopedWriteLock guard (state->lock);
        releaseResources();
        auto& all = state->renderers;
        all.erase (std::remove (all.begin(), all.end(), this), all.end());
    }

    void prepareToPlay (double sampleRateIn, int maximumBlockIn, int channelsIn,
                        juce::AudioProcessor::ProcessingPrecision precision,
                        AlwaysNonRealtime nonRealtime) override
    {
        juce::ARARenderer::prepareToPlay (sampleRateIn, maximumBlockIn, channelsIn, precision, nonRealtime);
        sampleRate = sampleRateIn;
        maximumBlock = maximumBlockIn;
        channels = channelsIn;
        useBufferedReaders = nonRealtime == AlwaysNonRealtime::no;
        prepared = true;
        readers.clear();
        refreshSources();
    }

    // Called on the document thread, never the audio callback. Keep existing
    // caches when audio is unchanged; rebuild invalidated source readers after
    // edits and add/remove sources as the host changes renderer assignments.
    void refreshSources()
    {
        if (! prepared) return;
        const juce::ScopedWriteLock guard (state->lock);
        std::set<juce::ARAAudioSource*> used;
        for (auto* region : getPlaybackRegions())
        {
            auto* source = region->getAudioModification()->getAudioSource();
            used.insert (source);
            auto found = readers.find (source);
            if (found != readers.end() && ! found->second.raw->isValid()) readers.erase (found);
            if (readers.find (source) == readers.end() && source->isSampleAccessEnabled())
            {
                SourceState entry;
                auto sourceReader = std::make_unique<juce::ARAAudioSourceReader> (source);
                entry.raw = sourceReader.get();
                if (useBufferedReaders)
                {
                    const int readAhead = juce::jmax (4 * maximumBlock, juce::roundToInt (2.0 * source->getSampleRate()));
                    entry.reader = std::make_unique<juce::BufferingAudioReader> (
                        sourceReader.release(), *readThread, readAhead);
                }
                else entry.reader = std::move (sourceReader);
                entry.playback.prepare (*entry.reader, sampleRate, maximumBlock);
                readers.emplace (source, std::move (entry));
            }
        }
        for (auto it = readers.begin(); it != readers.end();)
            if (used.count (it->first) == 0) it = readers.erase (it); else ++it;
    }

    void releaseResources() override
    {
        readers.clear();
        prepared = false;
    }

    bool processBlock (juce::AudioBuffer<float>& buffer,
                       juce::AudioProcessor::Realtime realtime,
                       const juce::AudioPlayHead::PositionInfo& position) noexcept override
    {
        buffer.clear();
        const juce::ScopedTryReadLock guard (state->lock);
        if (! guard.isLocked()) return false;
        if (! prepared || maximumBlock <= 0 || buffer.getNumChannels() != channels) return false;

        const auto blockStart = position.getTimeInSamples().orFallback (
            (juce::int64) std::llround (position.getTimeInSeconds().orFallback (0.0) * sampleRate));
        const auto blockRange = juce::Range<juce::int64>::withStartAndLength (blockStart, buffer.getNumSamples());
        if (! position.getIsPlaying()) return true;

        bool rendered = false;
        bool success = true;
        for (auto* region : getPlaybackRegions())
        {
            const auto playbackRange = region->getSampleRange (sampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);
            auto renderRange = blockRange.getIntersectionWith (playbackRange);
            const juce::Range<juce::int64> modificationRange { region->getStartInAudioModificationSamples(),
                                                               region->getEndInAudioModificationSamples() };
            if (renderRange.isEmpty()) continue;

            auto* source = region->getAudioModification()->getAudioSource();
            const auto reader = readers.find (source);
            if (reader == readers.end() || ! source->isSampleAccessEnabled()
                || region->isTimestretchEnabled() || region->isTimeStretchReflectingTempo()
                || std::abs (region->getDurationInAudioModificationTime() - region->getDurationInPlaybackTime()) > 1.0 / source->getSampleRate())
            {
                success = false;
                continue;
            }

            const int count = (int) renderRange.getLength();
            const int destination = (int) (renderRange.getStart() - blockRange.getStart());
            const double sourceStart = (double) modificationRange.getStart()
                + (double) (renderRange.getStart() - playbackRange.getStart()) * source->getSampleRate() / sampleRate;
            if (auto* buffered = dynamic_cast<juce::BufferingAudioReader*> (reader->second.reader.get()))
                buffered->setReadTimeout (realtime == juce::AudioProcessor::Realtime::yes ? 0 : 5000);
            bool readOK = true;
            for (int offset = 0; offset < count; offset += maximumBlock)
            {
                const int slice = juce::jmin (maximumBlock, count - offset);
                readOK &= reader->second.playback.add (buffer, destination + offset, slice,
                    sourceStart + offset * source->getSampleRate() / sampleRate,
                    modificationRange.getStart(), modificationRange.getEnd());
            }
            if (! readOK) { success = false; continue; }
            rendered = true;
        }
        if (! rendered) buffer.clear();
        return success;
    }

    using juce::ARAPlaybackRenderer::processBlock;

protected:
    void didAddPlaybackRegion (ARA::PlugIn::PlaybackRegion*) noexcept override { refreshSources(); }
    void didRemovePlaybackRegion (ARA::PlugIn::PlaybackRegion*) noexcept override { refreshSources(); }

private:
    struct SourceState
    {
        juce::ARAAudioSourceReader* raw = nullptr;
        std::unique_ptr<juce::AudioFormatReader> reader;
        sattari::arrangement::ClipPlayback playback;
    };
    std::shared_ptr<SattariARAModelState> state;
    juce::SharedResourcePointer<SattariARAReadThread> readThread;
    std::map<juce::ARAAudioSource*, SourceState> readers;
    bool useBufferedReaders = true, prepared = false;
    double sampleRate = 48000.0;
    int maximumBlock = 0, channels = 0;
};

class SattariARADocumentController final : public juce::ARADocumentControllerSpecialisation
{
public:
    using juce::ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

protected:
    void willBeginEditing (juce::ARADocument*) override { state->lock.enterWrite(); }
    void didEndEditing (juce::ARADocument*) override
    {
        for (auto* renderer : state->renderers) renderer->refreshSources();
        state->lock.exitWrite();
    }
    void didEnableAudioSourceSamplesAccess (juce::ARAAudioSource*, bool enable) override
    {
        if (enable)
            for (auto* renderer : state->renderers) renderer->refreshSources();
    }

    juce::ARAAudioModification* doCreateAudioModification (
        juce::ARAAudioSource* source, ARA::ARAAudioModificationHostRef hostRef,
        const juce::ARAAudioModification* clone) noexcept override
    {
        return new juce::ARAAudioModification (source, hostRef, clone);
    }

    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override
    {
        return new SattariARAPlaybackRenderer (getDocumentController(), state);
    }

    bool doRestoreObjectsFromStream (juce::ARAInputStream&, const juce::ARARestoreObjectsFilter*) noexcept override
    {
        getDocumentController()->getHostArchivingController()->notifyDocumentUnarchivingProgress (1.0f);
        return true;
    }

    bool doStoreObjectsToStream (juce::ARAOutputStream&, const juce::ARAStoreObjectsFilter*) noexcept override
    {
        getDocumentController()->getHostArchivingController()->notifyDocumentArchivingProgress (1.0f);
        return true;
    }

private:
    std::shared_ptr<SattariARAModelState> state = std::make_shared<SattariARAModelState>();
};
}

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<SattariARADocumentController>();
}

#endif
