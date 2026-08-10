#include "RetrospectiveRecorder.h"

RetrospectiveRecorder::RetrospectiveRecorder()
    : juce::Thread ("StemDeck retrospective recorder")
{
}

RetrospectiveRecorder::~RetrospectiveRecorder()
{
    prepared.store (false, std::memory_order_release);
    signalThreadShouldExit();
    notify();
    stopThread (3000);
}

void RetrospectiveRecorder::prepare (double sampleRate, int channels,
                                     int blockSize, double historySeconds)
{
    // Device changes may call prepare more than once. Stop the consumer before
    // replacing its queues and buffers so it can never observe a half-rebuilt pool.
    prepared.store (false, std::memory_order_release);
    if (isThreadRunning())
    {
        signalThreadShouldExit();
        notify();
        stopThread (3000);
    }

    {
        const juce::ScopedLock lock (historyLock);
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        currentChannels = juce::jlimit (1, 8, channels);
        preparedBlockSize = juce::jmax (1, blockSize);
        const int capacity = juce::jmax (
            preparedBlockSize,
            (int) std::ceil (currentSampleRate
                             * juce::jlimit (5.0, 300.0, historySeconds)));
        history.setSize (currentChannels, capacity, false, true, false);
        history.clear();
        writePosition = 0;
        validSamples = 0;
        publishedValidSamples.store (0, std::memory_order_relaxed);
        droppedBlocks.store (0, std::memory_order_relaxed);
        rebuildPool();
    }

    startThread (juce::Thread::Priority::low);
    prepared.store (true, std::memory_order_release);
}

void RetrospectiveRecorder::rebuildPool()
{
    blockPool.clear();
    blockPool.reserve (queueBlocks);
    for (int i = 0; i < queueBlocks; ++i)
    {
        auto block = std::make_unique<Block>();
        block->audio.setSize (currentChannels, preparedBlockSize,
                              false, true, false);
        blockPool.push_back (std::move (block));
    }
    resetQueues();
    for (const auto& block : blockPool)
        pushQueue (freeFifo, freeQueue, block.get());
}

void RetrospectiveRecorder::resetQueues() noexcept
{
    freeFifo.reset();
    pendingFifo.reset();
    freeQueue.fill (nullptr);
    pendingQueue.fill (nullptr);
}

RetrospectiveRecorder::Block* RetrospectiveRecorder::popQueue (
    juce::AbstractFifo& fifo,
    std::array<Block*, queueBlocks>& queue) noexcept
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead (1, start1, size1, start2, size2);
    if (size1 + size2 == 0) return nullptr;
    const int index = size1 > 0 ? start1 : start2;
    auto* result = queue[(size_t) index];
    queue[(size_t) index] = nullptr;
    fifo.finishedRead (1);
    return result;
}

bool RetrospectiveRecorder::pushQueue (
    juce::AbstractFifo& fifo,
    std::array<Block*, queueBlocks>& queue, Block* block) noexcept
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 + size2 == 0) return false;
    queue[(size_t) (size1 > 0 ? start1 : start2)] = block;
    fifo.finishedWrite (1);
    return true;
}

void RetrospectiveRecorder::captureBlock (
    const juce::AudioBuffer<float>& source)
{
    if (! prepared.load (std::memory_order_acquire)
        || source.getNumChannels() <= 0 || source.getNumSamples() <= 0)
        return;

    auto* block = popQueue (freeFifo, freeQueue);
    if (block == nullptr)
    {
        droppedBlocks.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    block->samples = juce::jmin (preparedBlockSize, source.getNumSamples());
    for (int channel = 0; channel < currentChannels; ++channel)
        block->audio.copyFrom (
            channel, 0, source,
            juce::jmin (channel, source.getNumChannels() - 1),
            0, block->samples);

    if (! pushQueue (pendingFifo, pendingQueue, block))
    {
        droppedBlocks.fetch_add (1, std::memory_order_relaxed);
        pushQueue (freeFifo, freeQueue, block);
        return;
    }
    notify();
}

void RetrospectiveRecorder::run()
{
    while (! threadShouldExit())
    {
        if (auto* block = popQueue (pendingFifo, pendingQueue))
        {
            {
                const juce::ScopedLock lock (historyLock);
                const int capacity = history.getNumSamples();
                const int first = juce::jmin (block->samples,
                                              capacity - writePosition);
                const int second = block->samples - first;
                for (int channel = 0; channel < currentChannels; ++channel)
                {
                    history.copyFrom (channel, writePosition,
                                      block->audio, channel, 0, first);
                    if (second > 0)
                        history.copyFrom (channel, 0, block->audio,
                                          channel, first, second);
                }
                writePosition = (writePosition + block->samples) % capacity;
                validSamples = juce::jmin (capacity,
                                           validSamples + block->samples);
                publishedValidSamples.store (validSamples,
                                             std::memory_order_release);
            }
            pushQueue (freeFifo, freeQueue, block);
            continue;
        }
        wait (20);
    }
}

double RetrospectiveRecorder::getAvailableSeconds() const noexcept
{
    return currentSampleRate > 0.0
        ? (double) publishedValidSamples.load (std::memory_order_acquire)
            / currentSampleRate
        : 0.0;
}

bool RetrospectiveRecorder::saveLastSeconds (
    const juce::File& destination, double seconds, juce::String& error)
{
    juce::AudioBuffer<float> snapshot;
    {
        const juce::ScopedLock lock (historyLock);
        const int count = juce::jmin (
            validSamples,
            (int) std::ceil (juce::jmax (0.1, seconds)
                             * currentSampleRate));
        if (count <= 0)
        {
            error = "No retrospective audio is available yet";
            return false;
        }
        snapshot.setSize (currentChannels, count);
        const int capacity = history.getNumSamples();
        const int start = (writePosition - count + capacity) % capacity;
        const int first = juce::jmin (count, capacity - start);
        const int second = count - first;
        for (int channel = 0; channel < currentChannels; ++channel)
        {
            snapshot.copyFrom (channel, 0, history, channel, start, first);
            if (second > 0)
                snapshot.copyFrom (channel, first, history, channel, 0, second);
        }
    }

    // Write beside the destination and atomically replace it only after the WAV is
    // complete. This avoids appending to an existing file and preserves the user's
    // previous take if disk IO fails halfway through.
    juce::TemporaryFile temporary (destination);
    {
        std::unique_ptr<juce::OutputStream> stream = temporary.getFile().createOutputStream();
        if (stream == nullptr)
        {
            error = "Could not create " + destination.getFullPathName();
            return false;
        }
        juce::WavAudioFormat wav;
        juce::AudioFormatWriterOptions options;
        options = options.withSampleRate (currentSampleRate)
                         .withNumChannels (currentChannels)
                         .withBitsPerSample (24);
        auto writer = wav.createWriterFor (stream, options);
        if (writer == nullptr)
        {
            error = "Could not create the WAV writer";
            return false;
        }
        if (! writer->writeFromAudioSampleBuffer (
                snapshot, 0, snapshot.getNumSamples()))
        {
            error = "Could not write retrospective audio";
            return false;
        }
    }
    if (! temporary.overwriteTargetFileWithTemporary())
    {
        error = "Could not replace " + destination.getFullPathName();
        return false;
    }
    return true;
}
