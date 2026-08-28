#include <JuceHeader.h>

#if JucePlugin_Enable_ARA

#include <map>

namespace
{
class AutoPitchARAReadThread final : public juce::TimeSliceThread
{
public:
    AutoPitchARAReadThread() : juce::TimeSliceThread ("Sattari Auto Pitch ARA Reader")
    {
        startThread (juce::Thread::Priority::high);
    }
};

class AutoPitchARAPlaybackRenderer final : public juce::ARAPlaybackRenderer
{
public:
    AutoPitchARAPlaybackRenderer (ARA::PlugIn::DocumentController* controller,
                                  juce::ReadWriteLock& modelLockIn)
        : juce::ARAPlaybackRenderer (controller), modelLock (modelLockIn) {}

    void prepareToPlay (double sampleRateIn, int maximumBlockIn, int channelsIn,
                        juce::AudioProcessor::ProcessingPrecision precision,
                        AlwaysNonRealtime nonRealtime) override
    {
        juce::ARARenderer::prepareToPlay (sampleRateIn, maximumBlockIn, channelsIn, precision, nonRealtime);
        sampleRate = sampleRateIn;
        maximumBlock = maximumBlockIn;
        channels = channelsIn;
        useBufferedReaders = nonRealtime == AlwaysNonRealtime::no;
        scratch.setSize (channels, maximumBlock);
        readers.clear();
        for (auto* region : getPlaybackRegions())
        {
            auto* source = region->getAudioModification()->getAudioSource();
            if (readers.find (source) == readers.end())
            {
                auto sourceReader = std::make_unique<juce::ARAAudioSourceReader> (source);
                if (useBufferedReaders)
                {
                    const int readAhead = juce::jmax (4 * maximumBlock, juce::roundToInt (2.0 * sampleRate));
                    readers.emplace (source, std::make_unique<juce::BufferingAudioReader> (
                        sourceReader.release(), *readThread, readAhead));
                }
                else
                {
                    readers.emplace (source, std::move (sourceReader));
                }
            }
        }
    }

    void releaseResources() override
    {
        readers.clear();
        scratch.setSize (0, 0);
    }

    bool processBlock (juce::AudioBuffer<float>& buffer,
                       juce::AudioProcessor::Realtime,
                       const juce::AudioPlayHead::PositionInfo& position) noexcept override
    {
        const juce::ScopedTryReadLock guard (modelLock);
        if (! guard.isLocked()) return false;
        if (buffer.getNumSamples() > maximumBlock || buffer.getNumChannels() != channels) return false;

        const auto blockStart = position.getTimeInSamples().orFallback (0);
        const auto blockRange = juce::Range<juce::int64>::withStartAndLength (blockStart, buffer.getNumSamples());
        buffer.clear();
        if (! position.getIsPlaying()) return true;

        bool rendered = false;
        bool success = true;
        for (auto* region : getPlaybackRegions())
        {
            const auto playbackRange = region->getSampleRange (sampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);
            auto renderRange = blockRange.getIntersectionWith (playbackRange);
            const juce::Range<juce::int64> modificationRange { region->getStartInAudioModificationSamples(),
                                                               region->getEndInAudioModificationSamples() };
            const auto sourceOffset = modificationRange.getStart() - playbackRange.getStart();
            renderRange = renderRange.getIntersectionWith (modificationRange.movedToStartAt (playbackRange.getStart()));
            if (renderRange.isEmpty()) continue;

            auto* source = region->getAudioModification()->getAudioSource();
            const auto reader = readers.find (source);
            if (reader == readers.end() || source->getChannelCount() != channels
                || std::abs (source->getSampleRate() - sampleRate) > 0.01)
            {
                success = false;
                continue;
            }

            const int count = (int) renderRange.getLength();
            const int destination = (int) (renderRange.getStart() - blockRange.getStart());
            const auto sourceStart = renderRange.getStart() + sourceOffset;
            scratch.clear();
            if (auto* buffered = dynamic_cast<juce::BufferingAudioReader*> (reader->second.get()))
                buffered->setReadTimeout (0);
            if (! reader->second->read (&scratch, destination, count, sourceStart, true, true))
            {
                success = false;
                continue;
            }
            for (int channel = 0; channel < channels; ++channel)
                buffer.addFrom (channel, destination, scratch, channel, destination, count);
            rendered = true;
        }
        if (! rendered) buffer.clear();
        return success;
    }

    using juce::ARAPlaybackRenderer::processBlock;

private:
    juce::ReadWriteLock& modelLock;
    juce::SharedResourcePointer<AutoPitchARAReadThread> readThread;
    std::map<juce::ARAAudioSource*, std::unique_ptr<juce::AudioFormatReader>> readers;
    juce::AudioBuffer<float> scratch;
    bool useBufferedReaders = true;
    double sampleRate = 48000.0;
    int maximumBlock = 0, channels = 0;
};

class AutoPitchARADocumentController final : public juce::ARADocumentControllerSpecialisation
{
public:
    using juce::ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

protected:
    void willBeginEditing (juce::ARADocument*) override { modelLock.enterWrite(); }
    void didEndEditing (juce::ARADocument*) override { modelLock.exitWrite(); }

    juce::ARAAudioModification* doCreateAudioModification (
        juce::ARAAudioSource* source, ARA::ARAAudioModificationHostRef hostRef,
        const juce::ARAAudioModification* clone) noexcept override
    {
        return new juce::ARAAudioModification (source, hostRef, clone);
    }

    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override
    {
        return new AutoPitchARAPlaybackRenderer (getDocumentController(), modelLock);
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
    juce::ReadWriteLock modelLock;
};
}

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<AutoPitchARADocumentController>();
}

#endif
