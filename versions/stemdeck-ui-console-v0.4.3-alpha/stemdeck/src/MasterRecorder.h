#pragma once

#include <JuceHeader.h>
#include <deque>
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

private:
    static constexpr int maxQueuedBlocks = 128;

    std::atomic<bool> recording { false };
    double currentSampleRate = 44100.0;
    int channels = 2;
    int poolBlockSize = 0;
    juce::File targetFile { juce::String{} };

    juce::CriticalSection queueLock;
    // Pool of pre-sized buffers recycled between the audio thread (fills) and the
    // writer thread (drains + returns). Owned by blockPool for stable addresses.
    std::vector<std::unique_ptr<juce::AudioBuffer<float>>> blockPool;
    std::deque<juce::AudioBuffer<float>*> freeBlocks;
    std::deque<juce::AudioBuffer<float>*> pendingBlocks;
    std::unique_ptr<juce::FileOutputStream> outputStream;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    std::atomic<int> droppedBlocks { 0 };

    void run() override;
    void closeWriter();
    void rebuildPool();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterRecorder)
};
