#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// Always-on, bounded master-history recorder. processBlock only copies into a
// preallocated SPSC queue; a worker owns the circular history buffer. Saving a
// snapshot copies the ordered tail under a short lock, then performs file IO after
// releasing it, so neither the audio callback nor history ingestion waits on disk.
class RetrospectiveRecorder final : private juce::Thread
{
public:
    RetrospectiveRecorder();
    ~RetrospectiveRecorder() override;

    void prepare (double sampleRate, int channels, int blockSize,
                  double historySeconds = 60.0);
    void captureBlock (const juce::AudioBuffer<float>&);

    bool saveLastSeconds (const juce::File&, double seconds,
                          juce::String& error);
    double getAvailableSeconds() const noexcept;
    int getDroppedBlockCount() const noexcept
    { return droppedBlocks.load (std::memory_order_relaxed); }

private:
    struct Block
    {
        juce::AudioBuffer<float> audio;
        int samples = 0;
    };

    static constexpr int queueBlocks = 128;
    std::vector<std::unique_ptr<Block>> blockPool;
    std::array<Block*, queueBlocks> freeQueue {};
    std::array<Block*, queueBlocks> pendingQueue {};
    juce::AbstractFifo freeFifo { queueBlocks };
    juce::AbstractFifo pendingFifo { queueBlocks };

    juce::AudioBuffer<float> history;
    mutable juce::CriticalSection historyLock;
    double currentSampleRate = 44100.0;
    int currentChannels = 2;
    int preparedBlockSize = 512;
    int writePosition = 0;
    int validSamples = 0;
    std::atomic<int> publishedValidSamples { 0 };
    std::atomic<int> droppedBlocks { 0 };
    std::atomic<bool> prepared { false };

    void run() override;
    void rebuildPool();
    void resetQueues() noexcept;
    static Block* popQueue (juce::AbstractFifo&,
                            std::array<Block*, queueBlocks>&) noexcept;
    static bool pushQueue (juce::AbstractFifo&,
                           std::array<Block*, queueBlocks>&, Block*) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RetrospectiveRecorder)
};
