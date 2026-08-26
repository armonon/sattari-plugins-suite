#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include "PluginSandboxProtocol.h"

struct StemDeckPluginSandboxTestAccess;

// Coordinator-side endpoint for one isolated third-party audio effect. The audio
// callback only touches a pre-mapped SPSC-ish ring and never waits for the worker.
// A late/dead helper therefore yields the dry block instead of a stage dropout.
class PluginSandboxClient final : private juce::ChildProcessCoordinator,
                                  private juce::Timer
{
public:
    using ReadyCallback = std::function<void (bool, juce::String)>;

    PluginSandboxClient();
    ~PluginSandboxClient() override;

    void prepare (double sampleRate, int blockSize, int channels);
    bool launch (const juce::PluginDescription&, ReadyCallback);
    void stop();
    void process (juce::AudioBuffer<float>&, const juce::MidiBuffer&) noexcept;

    bool isReady() const noexcept { return ready.load (std::memory_order_acquire); }
    bool hasCrashed() const noexcept { return crashed.load (std::memory_order_acquire); }
    int latencySamples() const noexcept { return preparedBlockSize; }
    juce::String pluginName() const;
    juce::String status() const;
    void setStateSnapshot (const juce::MemoryBlock&);
    juce::MemoryBlock getStateSnapshot() const;

    static juce::File locateHelperExecutable();
    static bool scanInHelper (juce::KnownPluginList&, juce::StringArray& messages,
                              int maxPluginsPerFormat, bool recursive);

private:
    friend struct StemDeckPluginSandboxTestAccess;
    bool launchInternal (const juce::PluginDescription&, ReadyCallback, bool recovery);
    void handleMessageFromWorker (const juce::MemoryBlock&) override;
    void handleConnectionLost() override;
    void timerCallback() override;
    bool createSharedFile();
    juce::MemoryBlock makeConfiguration (const juce::PluginDescription&) const;

    juce::File sharedFile;
    std::unique_ptr<juce::MemoryMappedFile> mapping;
    stemdeck::sandbox::SharedAudio* shared = nullptr;
    std::atomic<bool> ready { false }, crashed { false };
    std::atomic<std::uint32_t> nextSequence { 1 };
    int preparedBlockSize = 512, preparedChannels = 2;
    double preparedSampleRate = 44100.0;
    mutable juce::CriticalSection messageLock;
    juce::String loadedName, lastMessage;
    juce::MemoryBlock latestState;
    ReadyCallback readyCallback;
    juce::PluginDescription lastDescription;
    int restartAttempts = 0;
    std::atomic<bool> intentionalStop { false };
    std::shared_ptr<std::atomic<bool>> aliveFlag {
        std::make_shared<std::atomic<bool>> (true) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginSandboxClient)
};
