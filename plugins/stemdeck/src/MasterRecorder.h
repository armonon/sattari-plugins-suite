#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>

// Standalone-first master-output recorder.
// The audio callback copies finished master blocks into a bounded, pre-allocated
// pool (no heap traffic on the audio thread), and a background thread writes WAV
// data so disk I/O never happens in processBlock.
class MasterRecorder : private juce::Thread
{
public:
    MasterRecorder();
    ~MasterRecorder() override;

    void prepare (double sampleRate, int numChannels, int blockSize = 0);
    void reset();

    bool beginRecording (const juce::File& destinationFile);
    juce::File stopRecording();

    void captureBlock (const juce::AudioBuffer<float>& masterBuffer);

    bool isRecording() const noexcept { return recording.load(); }
    juce::File getTargetFile() const;
    int getDroppedBlockCount() const noexcept { return droppedBlocks.load (std::memory_order_relaxed); }

private:
    static constexpr int maxQueuedBlocks = 128;

    std::atomic<bool> recording { false };
    double currentSampleRate = 44100.0;
    int channels = 2;
    int poolBlockSize = 0;
    juce::File targetFile { juce::String{} };

    // Pool of pre-sized buffers recycled between the audio thread (fills) and the
    // writer thread (drains + returns). Owned by blockPool for stable addresses.
    std::vector<std::unique_ptr<juce::AudioBuffer<float>>> blockPool;
    std::array<juce::AudioBuffer<float>*, maxQueuedBlocks> freeQueue {};
    std::array<juce::AudioBuffer<float>*, maxQueuedBlocks> pendingQueue {};
    // free: writer -> audio. pending: audio -> writer. Each FIFO has exactly one
    // producer and one consumer, so AbstractFifo needs no external lock.
    juce::AbstractFifo freeFifo { maxQueuedBlocks };
    juce::AbstractFifo pendingFifo { maxQueuedBlocks };
    std::unique_ptr<juce::AudioFormatWriter> writer;
    std::atomic<int> droppedBlocks { 0 };

    void run() override;
    void closeWriter();
    void rebuildPool();
    void resetQueues() noexcept;
    static juce::AudioBuffer<float>* popQueue (
        juce::AbstractFifo&, std::array<juce::AudioBuffer<float>*, maxQueuedBlocks>&) noexcept;
    static bool pushQueue (
        juce::AbstractFifo&, std::array<juce::AudioBuffer<float>*, maxQueuedBlocks>&,
        juce::AudioBuffer<float>*) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterRecorder)
};
