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
    stop();
}

void PluginSandboxClient::prepare (double sampleRate, int blockSize, int channels)
{
    preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedBlockSize = juce::jlimit (1, stemdeck::sandbox::maxSamples, blockSize);
    preparedChannels = juce::jlimit (1, stemdeck::sandbox::maxChannels, channels);
    if (shared != nullptr)
    {
        shared->sampleRate = preparedSampleRate;
        shared->blockSize = (std::uint32_t) preparedBlockSize;
        shared->channels = (std::uint32_t) preparedChannels;
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
    shared = new (mapping->getData()) stemdeck::sandbox::SharedAudio();
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
    const auto utf8 = juce::JSON::toString (juce::var (object), true).toUTF8();
    return { utf8.getAddress(), (size_t) utf8.sizeInBytes() };
}

bool PluginSandboxClient::launch (const juce::PluginDescription& description,
                                  ReadyCallback callback)
{
    stop();
    {
        const juce::ScopedLock lock (messageLock);
        loadedName = description.name;
        lastMessage = {};
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
    ready.store (false, std::memory_order_release);
    if (shared != nullptr)
        stemdeck::sandbox::atomic (shared->shutdown).store (1, std::memory_order_release);
    killWorkerProcess();
    shared = nullptr;
    mapping.reset();
    if (sharedFile.existsAsFile()) sharedFile.deleteFile();
}

void PluginSandboxClient::process (juce::AudioBuffer<float>& audio) noexcept
{
    if (! ready.load (std::memory_order_acquire) || shared == nullptr
        || audio.getNumSamples() > stemdeck::sandbox::maxSamples
        || audio.getNumChannels() > stemdeck::sandbox::maxChannels)
        return;

    // Publish the current DRY input before consuming an older result. Reversing
    // these operations would accidentally feed the previous wet block back into
    // the plug-in and create an unintended feedback loop.
    for (auto& slot : shared->slots)
    {
        if (stemdeck::sandbox::atomic (slot.state).load (std::memory_order_acquire)
            != stemdeck::sandbox::free)
            continue;
        slot.sequence = nextSequence.fetch_add (1, std::memory_order_relaxed);
        slot.channels = (std::uint32_t) audio.getNumChannels();
        slot.samples = (std::uint32_t) audio.getNumSamples();
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            juce::FloatVectorOperations::copy (
                slot.audio + (size_t) ch * stemdeck::sandbox::maxSamples,
                audio.getReadPointer (ch), audio.getNumSamples());
        stemdeck::sandbox::atomic (slot.state).store (
            stemdeck::sandbox::inputReady, std::memory_order_release);
        break;
    }

    stemdeck::sandbox::AudioSlot* newest = nullptr;
    for (auto& slot : shared->slots)
        if (stemdeck::sandbox::atomic (slot.state).load (std::memory_order_acquire)
                == stemdeck::sandbox::outputReady
            && (newest == nullptr || slot.sequence > newest->sequence))
            newest = &slot;
    for (auto& slot : shared->slots)
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
    const bool ok = (bool) response.getProperty ("ok", false);
    const auto messageText = response.getProperty ("message", {}).toString();
    { const juce::ScopedLock lock (messageLock); lastMessage = messageText; }
    ready.store (ok, std::memory_order_release);
    if (readyCallback)
    {
        auto pendingCallback = std::move (readyCallback);
        juce::MessageManager::callAsync ([callbackToRun = std::move (pendingCallback), ok,
                                          messageText]() mutable
        { callbackToRun (ok, messageText); });
    }
}

void PluginSandboxClient::handleConnectionLost()
{
    const bool wasReady = ready.exchange (false, std::memory_order_acq_rel);
    crashed.store (wasReady, std::memory_order_release);
    const juce::ScopedLock lock (messageLock);
    lastMessage = wasReady ? loadedName + " sandbox exited; chain is dry-bypassed"
                           : "Plugin sandbox connection failed";
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
    const bool finished = process.waitForProcessToFinish (120000);
    const auto output = process.readAllProcessOutput().trim();
    if (output.isNotEmpty()) messages.add (output);
    if (! finished || process.getExitCode() != 0 || ! result.existsAsFile())
        return false;
    if (auto xml = juce::XmlDocument::parse (result))
        destination.recreateFromXml (*xml);
    result.deleteFile();
    return destination.getNumTypes() > 0;
}
