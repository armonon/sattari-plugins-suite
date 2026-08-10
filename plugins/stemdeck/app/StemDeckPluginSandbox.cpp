#include <JuceHeader.h>
#include "PluginSandboxProtocol.h"
#include <atomic>

namespace
{
constexpr auto workerId = "StemDeckSandbox1";

class SandboxWorker final : public juce::ChildProcessWorker,
                            private juce::Thread
{
public:
    SandboxWorker() : juce::Thread ("StemDeck sandbox audio")
    {
        juce::addDefaultFormatsToManager (formats);
    }
    ~SandboxWorker() override
    {
        signalThreadShouldExit();
        stopThread (2000);
    }

    void handleMessageFromCoordinator (const juce::MemoryBlock& message) override
    {
        const auto json = juce::JSON::parse (juce::String::fromUTF8 (
            (const char*) message.getData(), (int) message.getSize()));
        const auto file = juce::File (json.getProperty ("sharedFile", {}).toString());
        const auto pluginXmlText = json.getProperty ("plugin", {}).toString();
        const double sampleRate = (double) json.getProperty ("sampleRate", 44100.0);
        const int blockSize = (int) json.getProperty ("blockSize", 512);
        const int channels = (int) json.getProperty ("channels", 2);

        auto xml = juce::XmlDocument::parse (pluginXmlText);
        juce::PluginDescription description;
        juce::String error;
        bool ok = xml != nullptr && description.loadFromXml (*xml);
        if (ok)
            plugin = formats.createPluginInstance (description, sampleRate, blockSize, error);
        if (plugin != nullptr)
        {
            plugin->setPlayConfigDetails (channels, channels, sampleRate, blockSize);
            plugin->prepareToPlay (sampleRate, blockSize);
            mapping = std::make_unique<juce::MemoryMappedFile> (
                file, juce::MemoryMappedFile::readWrite, false);
            shared = mapping->getData() != nullptr
                       ? static_cast<stemdeck::sandbox::SharedAudio*> (mapping->getData())
                       : nullptr;
            ok = shared != nullptr && shared->fileMagic == stemdeck::sandbox::magic
                                  && shared->fileVersion == stemdeck::sandbox::version;
        }
        if (ok)
        {
            stemdeck::sandbox::atomic (shared->helperAlive).store (1, std::memory_order_release);
            startThread (juce::Thread::Priority::highest);
        }
        auto response = new juce::DynamicObject();
        response->setProperty ("ok", ok);
        response->setProperty ("message", ok ? description.name + " loaded in helper process"
                                              : "Sandbox load failed: " + error);
        const auto encoded = juce::JSON::toString (juce::var (response)).toUTF8();
        sendMessageToCoordinator ({ encoded.getAddress(), (size_t) encoded.sizeInBytes() });
    }

    void handleConnectionLost() override
    {
        juce::MessageManager::getInstance()->stopDispatchLoop();
    }

private:
    void run() override
    {
        juce::MidiBuffer midi;
        while (! threadShouldExit() && shared != nullptr
               && stemdeck::sandbox::atomic (shared->shutdown).load (std::memory_order_acquire) == 0)
        {
            bool didWork = false;
            for (auto& slot : shared->slots)
            {
                if (stemdeck::sandbox::atomic (slot.state).load (std::memory_order_acquire)
                    != stemdeck::sandbox::inputReady)
                    continue;
                const int channels = juce::jlimit (1, stemdeck::sandbox::maxChannels,
                                                   (int) slot.channels);
                const int samples = juce::jlimit (1, stemdeck::sandbox::maxSamples,
                                                  (int) slot.samples);
                std::array<float*, stemdeck::sandbox::maxChannels> pointers {};
                for (int ch = 0; ch < channels; ++ch)
                    pointers[(size_t) ch] = slot.audio + (size_t) ch * stemdeck::sandbox::maxSamples;
                juce::AudioBuffer<float> audio (pointers.data(), channels, samples);
                midi.clear();
                plugin->processBlock (audio, midi);
                bool valid = true;
                for (int ch = 0; ch < channels && valid; ++ch)
                    for (int i = 0; i < samples; ++i)
                        if (! std::isfinite (audio.getSample (ch, i))
                            || std::abs (audio.getSample (ch, i)) > 32.0f)
                        { valid = false; break; }
                stemdeck::sandbox::atomic (slot.state).store (
                    valid ? stemdeck::sandbox::outputReady : stemdeck::sandbox::free,
                    std::memory_order_release);
                didWork = true;
            }
            if (! didWork) wait (1);
        }
        if (shared != nullptr)
            stemdeck::sandbox::atomic (shared->helperAlive).store (0, std::memory_order_release);
    }

    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<juce::MemoryMappedFile> mapping;
    stemdeck::sandbox::SharedAudio* shared = nullptr;
};

int runScan (const juce::StringArray& args)
{
    if (args.size() < 3) return 2;
    const juce::File result (args[1]);
    const int limit = juce::jmax (1, args[2].getIntValue());
    const bool recursive = args.size() < 4 || args[3].getIntValue() != 0;
    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);
    juce::KnownPluginList list;
    const auto pedal = result.getSiblingFile (result.getFileNameWithoutExtension() + "-pedal.txt");
    for (auto* format : formats.getFormats())
    {
        juce::PluginDirectoryScanner scanner (list, *format,
            format->getDefaultLocationsToSearch(), recursive, pedal, false);
        juce::String current;
        int scanned = 0;
        while (scanned++ < limit && scanner.scanNextFile (true, current)) {}
    }
    if (auto xml = list.createXml())
        return xml->writeTo (result) ? 0 : 3;
    return 4;
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));
    if (args.contains ("--scan"))
        return runScan (args);

    SandboxWorker worker;
    if (! worker.initialiseFromCommandLine (juce::JUCEApplicationBase::getCommandLineParameters(),
                                             workerId, 4000))
        return 1;
    juce::MessageManager::getInstance()->runDispatchLoop();
    return 0;
}
