#include "PluginSandboxClient.h"

namespace
{
constexpr auto workerId = "StemDeckSandbox1";

juce::File sandboxDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("StemDeckPluginSandbox");
    dir.createDirectory();
    return dir;
}
}

PluginSandboxClient::PluginSandboxClient() = default;

PluginSandboxClient::~PluginSandboxClient()
{
    aliveFlag->store (false, std::memory_order_release);
    stop();
}

void PluginSandboxClient::prepare (double sampleRate, int blockSize, int channels)
{
    preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedBlockSize = juce::jlimit (1, stemdeck::sandbox::maxSamples, blockSize);
    preparedChannels = juce::jlimit (1, stemdeck::sandbox::maxChannels, channels);
    if (auto* sharedNow = shared.load (std::memory_order_acquire))
    {
        sharedNow->sampleRate = preparedSampleRate;
        sharedNow->blockSize = (std::uint32_t) preparedBlockSize;
        sharedNow->channels = (std::uint32_t) preparedChannels;
    }
}

juce::File PluginSandboxClient::locateHelperExecutable()
{
    if (const auto configured = juce::SystemStats::getEnvironmentVariable (
            "STEMDECK_PLUGIN_SANDBOX", {}); configured.isNotEmpty())
        return juce::File (configured);

    const auto current = juce::File::getSpecialLocation (
        juce::File::currentExecutableFile);
    for (const auto& candidate : {
             current.getSiblingFile ("StemDeck Plugin Sandbox"),
             current.getSiblingFile ("StemDeckPluginSandbox"),
             current.getParentDirectory().getChildFile ("Helpers")
                    .getChildFile ("StemDeck Plugin Sandbox") })
        if (candidate.existsAsFile()) return candidate;
    return {};
}

bool PluginSandboxClient::createSharedFile()
{
    sharedFile = sandboxDirectory().getNonexistentChildFile (
        "audio-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()),
        ".sdipc", false);
    {
        juce::FileOutputStream stream (sharedFile);
        if (! stream.openedOk()
            || ! stream.setPosition ((juce::int64) sizeof (stemdeck::sandbox::SharedAudio) - 1)
            || ! stream.writeByte (0))
            return false;
        stream.flush();
    }
    mapping = std::make_unique<juce::MemoryMappedFile> (
        sharedFile, juce::MemoryMappedFile::readWrite, false);
    if (mapping->getData() == nullptr
        || mapping->getSize() < sizeof (stemdeck::sandbox::SharedAudio))
        return false;
    std::memset (mapping->getData(), 0, sizeof (stemdeck::sandbox::SharedAudio));
    shared.store (new (mapping->getData()) stemdeck::sandbox::SharedAudio(),
                  std::memory_order_release);
    prepare (preparedSampleRate, preparedBlockSize, preparedChannels);
    return true;
}

juce::MemoryBlock PluginSandboxClient::makeConfiguration (
    const juce::PluginDescription& description) const
{
    auto object = new juce::DynamicObject();
    object->setProperty ("sharedFile", sharedFile.getFullPathName());
    object->setProperty ("sampleRate", preparedSampleRate);
    object->setProperty ("blockSize", preparedBlockSize);
    object->setProperty ("channels", preparedChannels);
    if (auto xml = description.createXml())
        object->setProperty ("plugin", xml->toString());
    {
        const juce::ScopedLock lock (messageLock);
        if (latestState.getSize() > 0)
            object->setProperty ("state", latestState.toBase64Encoding());
    }
    const auto utf8 = juce::JSON::toString (juce::var (object), true).toUTF8();
    return { utf8.getAddress(), (size_t) utf8.sizeInBytes() };
}

bool PluginSandboxClient::launch (const juce::PluginDescription& description,
                                  ReadyCallback callback)
{
    return launchInternal (description, std::move (callback), false);
}

bool PluginSandboxClient::launchInternal (const juce::PluginDescription& description,
                                          ReadyCallback callback, bool recovery)
{
    stop();
    if (! recovery)
    {
        restartAttempts = 0;
        lastDescription = description;
    }
    {
        const juce::ScopedLock lock (messageLock);
        loadedName = description.name;
        lastMessage = recovery ? "Restarting " + description.name + " in the sandbox" : juce::String{};
    }
    readyCallback = std::move (callback);
    const auto fail = [this] (juce::String failureText)
    {
        { const juce::ScopedLock lock (messageLock); lastMessage = failureText; }
        if (readyCallback)
        {
            auto pendingCallback = std::move (readyCallback);
            juce::MessageManager::callAsync (
                [callbackToRun = std::move (pendingCallback),
                 deliveredMessage = std::move (failureText)]() mutable
                { callbackToRun (false, deliveredMessage); });
        }
        return false;
    };
    const auto helper = locateHelperExecutable();
    if (! helper.existsAsFile())
        return fail ("StemDeck Plugin Sandbox helper is not installed");
    if (! createSharedFile())
        return fail ("Could not create the sandbox audio bridge");
    crashed.store (false, std::memory_order_release);
    ready.store (false, std::memory_order_release);
    if (! launchWorkerProcess (helper, workerId, 4000,
                               juce::ChildProcess::wantStdOut
                                 | juce::ChildProcess::wantStdErr))
        return fail ("Could not launch the plugin sandbox helper");
    if (! sendMessageToWorker (makeConfiguration (description)))
        return fail ("Could not configure the plugin sandbox helper");
    return true;
}

void PluginSandboxClient::stop()
{
    stopTimer();
    intentionalStop.store (true, std::memory_order_release);
    ready.store (false, std::memory_order_release);
    if (auto* sharedNow = shared.load (std::memory_order_acquire))
        stemdeck::sandbox::atomic (sharedNow->shutdown).store (1, std::memory_order_release);
    killWorkerProcess();

    // Retire the pointer FIRST so no new process() call can enter, then wait for any
    // reader already inside to leave before the mapping is destroyed. Without this the
    // crash-recovery relaunch (launchInternal with recovery=true, which runs stop()
    // outside HostGraph's graphLock) can munmap the pages while the audio thread is
    // mid-loop over shared->slots — SIGBUS in the audio callback.
    //
    // The wait is on the message thread and bounded: a reader is inside for at most one
    // audio block. The cap means a pathological stall leaks the mapping rather than
    // hanging the UI, which is the right way round.
    shared.store (nullptr, std::memory_order_release);
    for (int spins = 0; processActive.load (std::memory_order_acquire) && spins < 200; ++spins)
        juce::Thread::sleep (1);
    if (processActive.load (std::memory_order_acquire))
    {
        // Deliberately leak rather than unmap under a live reader.
        jassertfalse;
        (void) mapping.release();
    }
    mapping.reset();
    if (sharedFile.existsAsFile()) sharedFile.deleteFile();
    intentionalStop.store (false, std::memory_order_release);
}

void PluginSandboxClient::process (juce::AudioBuffer<float>& audio,
                                   const juce::MidiBuffer& midi) noexcept
{
    // Mark the mapping in use BEFORE reading the pointer. Ordering matters: if the flag
    // were set after the null check, stop() could observe processActive == false in the
    // window between the check and the first dereference and unmap underneath us. Pure
    // atomic stores — no allocation, no lock, RT-safe.
    struct ActiveGuard
    {
        std::atomic<bool>& flag;
        explicit ActiveGuard (std::atomic<bool>& f) noexcept : flag (f)
        { flag.store (true, std::memory_order_release); }
        ~ActiveGuard() noexcept { flag.store (false, std::memory_order_release); }
    } activeGuard { processActive };

    auto* const sharedNow = shared.load (std::memory_order_acquire);
    if (! ready.load (std::memory_order_acquire) || sharedNow == nullptr
        || audio.getNumSamples() > stemdeck::sandbox::maxSamples
        || audio.getNumChannels() > stemdeck::sandbox::maxChannels)
        return;

    // Publish the current DRY input before consuming an older result. Reversing
    // these operations would accidentally feed the previous wet block back into
    // the plug-in and create an unintended feedback loop.
    for (auto& slot : sharedNow->slots)
    {
        if (stemdeck::sandbox::atomic (slot.state).load (std::memory_order_acquire)
            != stemdeck::sandbox::free)
            continue;
        slot.sequence = nextSequence.fetch_add (1, std::memory_order_relaxed);
        slot.channels = (std::uint32_t) audio.getNumChannels();
        slot.samples = (std::uint32_t) audio.getNumSamples();
        slot.midiCount = 0;
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            const int size = message.getRawDataSize();
            if (slot.midiCount >= stemdeck::sandbox::maxMidiEvents
                || size <= 0 || size > stemdeck::sandbox::maxMidiBytes)
                continue;
            auto& event = slot.midi[slot.midiCount++];
            event.frame = (std::uint16_t) juce::jlimit (
                0, juce::jmax (0, audio.getNumSamples() - 1),
                metadata.samplePosition);
            event.size = (std::uint8_t) size;
            std::memcpy (event.data, message.getRawData(), (size_t) size);
        }
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            juce::FloatVectorOperations::copy (
                slot.audio + (size_t) ch * stemdeck::sandbox::maxSamples,
                audio.getReadPointer (ch), audio.getNumSamples());
        stemdeck::sandbox::atomic (slot.state).store (
            stemdeck::sandbox::inputReady, std::memory_order_release);
        break;
    }

    stemdeck::sandbox::AudioSlot* newest = nullptr;
    for (auto& slot : sharedNow->slots)
        if (stemdeck::sandbox::atomic (slot.state).load (std::memory_order_acquire)
                == stemdeck::sandbox::outputReady
            && (newest == nullptr || slot.sequence > newest->sequence))
            newest = &slot;
    for (auto& slot : sharedNow->slots)
        if (&slot != newest
            && stemdeck::sandbox::atomic (slot.state).load (std::memory_order_acquire)
                   == stemdeck::sandbox::outputReady)
            stemdeck::sandbox::atomic (slot.state).store (
                stemdeck::sandbox::free, std::memory_order_release);
    if (newest != nullptr
        && newest->samples == (std::uint32_t) audio.getNumSamples())
    {
        const int channels = juce::jmin ((int) newest->channels,
                                         audio.getNumChannels());
        for (int ch = 0; ch < channels; ++ch)
            juce::FloatVectorOperations::copy (
                audio.getWritePointer (ch),
                newest->audio + (size_t) ch * stemdeck::sandbox::maxSamples,
                audio.getNumSamples());
        stemdeck::sandbox::atomic (newest->state).store (
            stemdeck::sandbox::free, std::memory_order_release);
    }
}

void PluginSandboxClient::handleMessageFromWorker (const juce::MemoryBlock& message)
{
    const auto text = juce::String::fromUTF8 ((const char*) message.getData(),
                                               (int) message.getSize());
    const auto response = juce::JSON::parse (text);
    if (response.getProperty ("type", {}).toString() == "state")
    {
        juce::MemoryBlock snapshot;
        snapshot.fromBase64Encoding (
            response.getProperty ("state", {}).toString());
        const juce::ScopedLock lock (messageLock);
        latestState = std::move (snapshot);
        return;
    }
    const bool ok = (bool) response.getProperty ("ok", false);
    const auto messageText = response.getProperty ("message", {}).toString();
    { const juce::ScopedLock lock (messageLock); lastMessage = messageText; }
    ready.store (ok, std::memory_order_release);
    // startTimer must not be called from here. ChildProcessCoordinator::Connection is
    // constructed with callbacksOnMessageThread == false, so handleMessageFromWorker runs
    // on the InterprocessConnection reader thread, while stop() calls stopTimer() on the
    // message thread. juce::Timer's timerPeriodMs and positionInQueue are plain non-atomic
    // members: concurrent start/stop can index the global TimerThread queue at a stale
    // position (a dangling Timer* callback in release builds, where the jassert is gone),
    // or leave the timer registered after stop() returned, firing on a half-destroyed
    // object. Marshal onto the message thread, guarded by the same aliveFlag token the
    // restart path already uses.
    if (ok)
    {
        const auto life = aliveFlag;
        juce::MessageManager::callAsync ([this, life]
        {
            if (! life->load (std::memory_order_acquire)) return;
            startTimer (1000);
        });
    }
    if (readyCallback)
    {
        auto pendingCallback = std::move (readyCallback);
        juce::MessageManager::callAsync ([callbackToRun = std::move (pendingCallback), ok,
                                          messageText]() mutable
        { callbackToRun (ok, messageText); });
    }
}

void PluginSandboxClient::timerCallback()
{
    if (! isReady()) return;
    auto request = new juce::DynamicObject();
    request->setProperty ("command", "state");
    const auto encoded = juce::JSON::toString (juce::var (request)).toUTF8();
    sendMessageToWorker ({ encoded.getAddress(), (size_t) encoded.sizeInBytes() });
}

void PluginSandboxClient::setStateSnapshot (const juce::MemoryBlock& snapshot)
{
    const juce::ScopedLock lock (messageLock);
    latestState = snapshot;
}

juce::MemoryBlock PluginSandboxClient::getStateSnapshot() const
{
    const juce::ScopedLock lock (messageLock);
    return latestState;
}

void PluginSandboxClient::handleConnectionLost()
{
    const bool wasReady = ready.exchange (false, std::memory_order_acq_rel);
    if (intentionalStop.load (std::memory_order_acquire))
        return;

    crashed.store (wasReady, std::memory_order_release);
    {
        const juce::ScopedLock lock (messageLock);
        lastMessage = wasReady ? loadedName + " sandbox exited; chain is dry-bypassed"
                               : "Plugin sandbox connection failed";
    }

    // A transient helper crash must not require a UI intervention mid-set. Retry
    // twice, outside the audio callback, then remain dry-bypassed. The lifetime
    // token makes both queued callbacks harmless if the chain is removed first.
    if (wasReady && restartAttempts < 2 && lastDescription.name.isNotEmpty())
    {
        const int attempt = ++restartAttempts;
        const auto description = lastDescription;
        const auto life = aliveFlag;
        juce::MessageManager::callAsync (
            [this, life, description, attempt]
            {
                if (! life->load (std::memory_order_acquire)) return;
                juce::Timer::callAfterDelay (attempt * 250,
                    [this, life, description]
                    {
                        if (! life->load (std::memory_order_acquire)) return;
                        launchInternal (description, {}, true);
                    });
            });
    }
}

juce::String PluginSandboxClient::pluginName() const
{
    const juce::ScopedLock lock (messageLock);
    return loadedName;
}

juce::String PluginSandboxClient::status() const
{
    const juce::ScopedLock lock (messageLock);
    if (hasCrashed()) return lastMessage;
    if (isReady()) return loadedName + " isolated and running";
    return lastMessage.isNotEmpty() ? lastMessage : "Sandbox idle";
}

bool PluginSandboxClient::scanInHelper (juce::KnownPluginList& destination,
                                        juce::StringArray& messages,
                                        int maxPluginsPerFormat, bool recursive)
{
    const auto helper = locateHelperExecutable();
    if (! helper.existsAsFile())
    {
        messages.add ("Plugin sandbox helper is not installed; third-party scan skipped");
        return false;
    }
    const auto result = sandboxDirectory().getNonexistentChildFile ("scan", ".xml", false);
    const auto args = juce::StringArray { helper.getFullPathName(), "--scan",
        result.getFullPathName(), juce::String (maxPluginsPerFormat), recursive ? "1" : "0" };
    juce::ChildProcess process;
    if (! process.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return false;
    // Was: waitForProcessToFinish(120000) followed unconditionally by
    // readAllProcessOutput(). Two ways that hangs the host forever, and this project has
    // already shipped this exact failure once (CLAUDE.md hard rules: "a ThreadPool job
    // that never calls shouldExit() is not interruptible, and removeAllJobs(true, -1)
    // will wait for it forever ... a potential 45-minute host freeze on quit"):
    //
    //   1. waitForProcessToFinish returns false on timeout WITHOUT killing the child, and
    //      readAllProcessOutput() then loops until the pipe closes — on a still-running
    //      child that never returns. One third-party plugin that hangs its scan is enough.
    //   2. Nothing polled shouldExit(), so quitting could not interrupt the wait.
    //
    // Poll instead: honour the deadline, honour job cancellation, and always kill the
    // child before reading, so the read is guaranteed to terminate.
    constexpr int kScanTimeoutMs = 120000;
    constexpr int kPollMs = 100;
    auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) kScanTimeoutMs;
    bool finished = false;
    bool cancelled = false;
    while (process.isRunning())
    {
        if (job != nullptr && job->shouldExit()) { cancelled = true; break; }
        if (juce::Time::getMillisecondCounter() >= deadline) break;
        juce::Thread::sleep (kPollMs);
    }
    finished = ! process.isRunning();

    if (! finished)
    {
        // Kill BEFORE reading. A live child means readAllProcessOutput never returns.
        process.kill();
        for (int i = 0; i < 20 && process.isRunning(); ++i)
            juce::Thread::sleep (25);
    }

    const auto output = process.readAllProcessOutput().trim();
    if (output.isNotEmpty()) messages.add (output);
    if (cancelled)
    {
        messages.add ("Third-party plugin scan cancelled");
        return false;
    }
    if (! finished)
    {
        messages.add ("Third-party plugin scan timed out after "
                      + juce::String (kScanTimeoutMs / 1000) + "s and was terminated");
        return false;
    }
    if (process.getExitCode() != 0 || ! result.existsAsFile())
        return false;
    if (auto xml = juce::XmlDocument::parse (result))
        destination.recreateFromXml (*xml);
    result.deleteFile();
    return destination.getNumTypes() > 0;
}
