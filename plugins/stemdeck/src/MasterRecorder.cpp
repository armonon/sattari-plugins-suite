#include "MasterRecorder.h"

MasterRecorder::MasterRecorder()
    : Thread ("StemDeck master recorder")
{
}

MasterRecorder::~MasterRecorder()
{
    reset();
}

void MasterRecorder::prepare (double sampleRate, int numChannels)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    channels = juce::jmax (1, numChannels);
}

void MasterRecorder::reset()
{
    recording.store (false);
    notify();
    stopThread (3000);

    {
        const juce::ScopedLock lock (queueLock);
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
        return false;

    outputStream = std::move (stream);
    writer = std::move (newWriter);
    targetFile = destinationFile;
    startThread (juce::Thread::Priority::normal);
    targetFile = destinationFile;
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
    if (! recording.load())
        return;

    if (! writer)
        return;

    const int maxQueuedBlocks = 128;
    const juce::ScopedTryLock lock (queueLock);
    if (! lock.isLocked())
    {
        droppedBlocks.fetch_add (1);
        return;
    }

    if ((int) pendingBlocks.size() >= maxQueuedBlocks)
    {
        droppedBlocks.fetch_add (1);
        return;
    }

    juce::AudioBuffer<float> copy (channels, masterBuffer.getNumSamples());
    for (int ch = 0; ch < channels; ++ch)
        copy.copyFrom (ch, 0, masterBuffer, juce::jmin (ch, masterBuffer.getNumChannels() - 1), 0, masterBuffer.getNumSamples());

    pendingBlocks.push_back (std::move (copy));
    notify();
}

juce::File MasterRecorder::getTargetFile() const
{
    return targetFile;
}

void MasterRecorder::run()
{
    while (! threadShouldExit())
    {
        juce::AudioBuffer<float> block;
        {
            const juce::ScopedLock lock (queueLock);
            if (! pendingBlocks.empty())
            {
                block = std::move (pendingBlocks.front());
                pendingBlocks.pop_front();
            }
        }

        if (block.getNumSamples() > 0 && writer)
            writer->writeFromAudioSampleBuffer (block, 0, block.getNumSamples());
        else
            wait (20);
    }

    for (;;)
    {
        juce::AudioBuffer<float> block;
        {
            const juce::ScopedLock lock (queueLock);
            if (pendingBlocks.empty())
                break;

            block = std::move (pendingBlocks.front());
            pendingBlocks.pop_front();
        }

        if (block.getNumSamples() > 0 && writer)
            writer->writeFromAudioSampleBuffer (block, 0, block.getNumSamples());
    }
}

void MasterRecorder::closeWriter()
{
    writer.reset();
    outputStream.reset();
}
