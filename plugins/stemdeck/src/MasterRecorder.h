#pragma once

#include <JuceHeader.h>
#include <deque>

// Standalone-first master-output recorder.
// The audio callback copies finished master blocks into a bounded queue, and a
// background thread writes WAV data so disk I/O never happens in processBlock.
class MasterRecorder : private juce::Thread
{
public:
    MasterRecorder();
    ~MasterRecorder() override;

    void prepare (double sampleRate, int numChannels);
    void reset();

    bool beginRecording (const juce::File& destinationFile);
    juce::File stopRecording();

    void captureBlock (const juce::AudioBuffer<float>& masterBuffer);

    bool isRecording() const noexcept { return recording.load(); }
    juce::File getTargetFile() const;

private:
    std::atomic<bool> recording { false };
    double currentSampleRate = 44100.0;
    int channels = 2;
    juce::File targetFile { juce::String{} };

    juce::CriticalSection queueLock;
    std::deque<juce::AudioBuffer<float>> pendingBlocks;
    std::unique_ptr<juce::FileOutputStream> outputStream;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    std::atomic<int> droppedBlocks { 0 };

    void run() override;
    void closeWriter();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterRecorder)
};
