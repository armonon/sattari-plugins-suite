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

    // Atomic, and paired with processActive below, because stop() unmaps this memory from
    // the message thread while the audio thread may be inside process() using it. Every
    // stop() except the crash-recovery relaunch happens under HostGraph's graphLock (which
    // processChain also takes), but launchInternal(..., recovery=true) calls stop() with no
    // lock held and PluginSandboxClient cannot take graphLock. Unmapping under a live
    // reader is a SIGBUS in the audio callback, on the crash path — the hardest place to
    // reproduce. See stop() for the handshake.
    std::atomic<stemdeck::sandbox::SharedAudio*> shared { nullptr };

    // Set while the audio thread is inside process(). stop() clears `shared` first, then
    // waits for this to fall before unmapping, so no reader can be holding the mapping.
    std::atomic<bool> processActive { false };
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
