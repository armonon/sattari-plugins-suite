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
    poolBlockSize = block;
    blockPool.clear();
    blockPool.reserve ((size_t) maxQueuedBlocks);
    for (int i = 0; i < maxQueuedBlocks; ++i)
        blockPool.push_back (std::make_unique<juce::AudioBuffer<float>> (channels, block));
    resetQueues();
}

juce::AudioBuffer<float>* MasterRecorder::popQueue (
    juce::AbstractFifo& fifo,
    std::array<juce::AudioBuffer<float>*, maxQueuedBlocks>& queue) noexcept
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead (1, start1, size1, start2, size2);
    if (size1 + size2 == 0)
        return nullptr;
    const int index = size1 > 0 ? start1 : start2;
    auto* result = queue[(size_t) index];
    fifo.finishedRead (1);
    return result;
}

bool MasterRecorder::pushQueue (
    juce::AbstractFifo& fifo,
    std::array<juce::AudioBuffer<float>*, maxQueuedBlocks>& queue,
    juce::AudioBuffer<float>* block) noexcept
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 + size2 == 0)
        return false;
    queue[(size_t) (size1 > 0 ? start1 : start2)] = block;
    fifo.finishedWrite (1);
    return true;
}

void MasterRecorder::resetQueues() noexcept
{
    freeFifo.reset();
    pendingFifo.reset();
    freeQueue.fill (nullptr);
    pendingQueue.fill (nullptr);
    for (const auto& block : blockPool)
        pushQueue (freeFifo, freeQueue, block.get());
}

void MasterRecorder::reset()
{
    recording.store (false);
    notify();
    stopThread (3000);

    resetQueues();

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

    // Never resize beyond the capacity negotiated in prepare(): that would allocate
    // on the real-time thread. A misbehaving host costs one reported dropped block,
    // not an audio callback stall.
    if (numSamples > poolBlockSize)
    {
        droppedBlocks.fetch_add (1);
        return;
    }

    auto* dst = popQueue (freeFifo, freeQueue);
    if (dst == nullptr)
    {
        droppedBlocks.fetch_add (1);
        return;
    }

    // setSize with avoidReallocating keeps the pool buffer's capacity (no RT alloc).
    dst->setSize (channels, numSamples, false, false, true);
    for (int ch = 0; ch < channels; ++ch)
        dst->copyFrom (ch, 0, masterBuffer, juce::jmin (ch, srcCh - 1), 0, numSamples);

    const bool writerWasIdle = pendingFifo.getNumReady() == 0;
    if (! pushQueue (pendingFifo, pendingQueue, dst))
    {
        pushQueue (freeFifo, freeQueue, dst);
        droppedBlocks.fetch_add (1);
        return;
    }
    if (writerWasIdle)
        notify();
}

juce::File MasterRecorder::getTargetFile() const
{
    return targetFile;
}

void MasterRecorder::run()
{
    double lastCheckpointMs = juce::Time::getMillisecondCounterHiRes();
    auto drainOne = [this] () -> bool
    {
        auto* block = popQueue (pendingFifo, pendingQueue);

        if (block == nullptr)
            return false;

        if (block->getNumSamples() > 0 && writer)
            writer->writeFromAudioSampleBuffer (*block, 0, block->getNumSamples());

        pushQueue (freeFifo, freeQueue, block);
        return true;
    };

    while (! threadShouldExit())
    {
        const bool wroteBlock = drainOne();
        const double now = juce::Time::getMillisecondCounterHiRes();
        // WavAudioFormatWriter normally finalises RIFF/data lengths only in its
        // destructor. Checkpointing the header on the disk thread keeps an active
        // take readable after a power loss or force-quit, with at most one second
        // of uncheckpointed tail. No file I/O touches the audio callback.
        if (writer != nullptr && now - lastCheckpointMs >= 1000.0)
        {
            writer->flush();
            lastCheckpointMs = now;
        }
        if (! wroteBlock)
            wait (20);
    }

    // Flush whatever remains after stop.
    while (drainOne()) {}
    if (writer != nullptr)
        writer->flush();
}

void MasterRecorder::closeWriter()
{
    writer.reset();
}
