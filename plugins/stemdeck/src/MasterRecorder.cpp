#include "MasterRecorder.h"

MasterRecorder::MasterRecorder()
    : Thread ("StemDeck master recorder")
{
}

MasterRecorder::~MasterRecorder()
{
    reset();
}

void MasterRecorder::prepare (double sampleRate, int numChannels, int blockSize)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    channels = juce::jmax (1, numChannels);
    if (blockSize > 0)
        poolBlockSize = blockSize;
    rebuildPool();
}

void MasterRecorder::rebuildPool()
{
    const int block = juce::jmax (1, poolBlockSize > 0 ? poolBlockSize : 2048);
    const juce::ScopedLock lock (queueLock);
    pendingBlocks.clear();
    freeBlocks.clear();
    blockPool.clear();
    blockPool.reserve ((size_t) maxQueuedBlocks);
    for (int i = 0; i < maxQueuedBlocks; ++i)
    {
        auto b = std::make_unique<juce::AudioBuffer<float>> (channels, block);
        freeBlocks.push_back (b.get());
        blockPool.push_back (std::move (b));
    }
}

void MasterRecorder::reset()
{
    recording.store (false);
    notify();
    stopThread (3000);

    {
        const juce::ScopedLock lock (queueLock);
        // Return every in-flight buffer to the free list (pool memory is retained).
        for (auto* b : pendingBlocks)
            freeBlocks.push_back (b);
        pendingBlocks.clear();
    }

    closeWriter();
    targetFile = juce::File (juce::String{});
    droppedBlocks.store (0);
}

bool MasterRecorder::beginRecording (const juce::File& destinationFile)
{
    if (destinationFile.getFullPathName().isEmpty())
        return false;

    reset();

    if (blockPool.empty())
        rebuildPool();

    auto parent = destinationFile.getParentDirectory();
    if (! parent.exists() && ! parent.createDirectory())
        return false;

    juce::WavAudioFormat wav;
    auto stream = destinationFile.createOutputStream();
    if (! stream)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> newWriter (wav.createWriterFor (stream.get(),
                                                                             currentSampleRate,
                                                                             (unsigned int) channels,
                                                                             24,
                                                                             {},
                                                                             0));
    if (! newWriter)
        return false;   // writer creation failed: `stream` still owns it, freed on scope exit

    // The AudioFormatWriter takes ownership of the stream and deletes it in its
    // destructor. Release it from the local unique_ptr so it isn't double-freed
    // (the old code stored it in `outputStream` too → double delete → crash on stop).
    stream.release();
    writer = std::move (newWriter);
    targetFile = destinationFile;
    startThread (juce::Thread::Priority::normal);
    recording.store (true);
    return true;
}

juce::File MasterRecorder::stopRecording()
{
    recording.store (false);
    notify();
    stopThread (3000);
    closeWriter();
    return targetFile;
}

void MasterRecorder::captureBlock (const juce::AudioBuffer<float>& masterBuffer)
{
    // `recording` (atomic) is the sole gate — the audio thread never touches the
    // `writer` unique_ptr, which the message thread resets in closeWriter().
    if (! recording.load())
        return;

    const int numSamples = masterBuffer.getNumSamples();
    const int srcCh = masterBuffer.getNumChannels();
    if (numSamples <= 0 || srcCh <= 0)
        return;

    const juce::ScopedTryLock lock (queueLock);
    if (! lock.isLocked())
    {
        droppedBlocks.fetch_add (1);
        return;
    }

    if (freeBlocks.empty())
    {
        droppedBlocks.fetch_add (1);
        return;
    }

    auto* dst = freeBlocks.front();
    freeBlocks.pop_front();

    // setSize with avoidReallocating keeps the pool buffer's capacity (no RT alloc).
    dst->setSize (channels, numSamples, false, false, true);
    for (int ch = 0; ch < channels; ++ch)
        dst->copyFrom (ch, 0, masterBuffer, juce::jmin (ch, srcCh - 1), 0, numSamples);

    pendingBlocks.push_back (dst);
    notify();
}

juce::File MasterRecorder::getTargetFile() const
{
    return targetFile;
}

void MasterRecorder::run()
{
    auto drainOne = [this] () -> bool
    {
        juce::AudioBuffer<float>* block = nullptr;
        {
            const juce::ScopedLock lock (queueLock);
            if (! pendingBlocks.empty())
            {
                block = pendingBlocks.front();
                pendingBlocks.pop_front();
            }
        }

        if (block == nullptr)
            return false;

        if (block->getNumSamples() > 0 && writer)
            writer->writeFromAudioSampleBuffer (*block, 0, block->getNumSamples());

        const juce::ScopedLock lock (queueLock);
        freeBlocks.push_back (block);
        return true;
    };

    while (! threadShouldExit())
        if (! drainOne())
            wait (20);

    // Flush whatever remains after stop.
    while (drainOne()) {}
}

void MasterRecorder::closeWriter()
{
    writer.reset();
    outputStream.reset();
}
