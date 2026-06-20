#include "HostGraph.h"

namespace
{
juce::StringArray pluginNamesForLog (const juce::Array<juce::PluginDescription>& plugins, int maxItems)
{
    juce::StringArray names;
    for (int i = 0; i < juce::jmin (plugins.size(), maxItems); ++i)
        names.add (plugins.getReference (i).name + " (" + plugins.getReference (i).pluginFormatName + ")");
    return names;
}

juce::File hostDataDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Sattari")
                   .getChildFile ("StemDeck")
                   .getChildFile ("HostGraph");
    dir.createDirectory();
    return dir;
}
}

HostGraph::HostGraph()
{
    // Current JUCE API: do not use deprecated AudioPluginFormatManager::addDefaultFormats().
#if JucePlugin_Build_Standalone
    juce::addDefaultFormatsToManager (formatManager);
#else
    juce::addHeadlessDefaultFormatsToManager (formatManager);
#endif
    loadCache();
}

HostGraph::~HostGraph()
{
    release();
}

bool HostGraph::standaloneHostingEnabled() const noexcept
{
#if JucePlugin_Build_Standalone
    return true;
#else
    return false;
#endif
}

juce::String HostGraph::chainId (Chain chain)
{
    switch (chain)
    {
        case Chain::deckA: return "deckA";
        case Chain::deckB: return "deckB";
        case Chain::deckC: return "deckC";
        case Chain::deckD: return "deckD";
        case Chain::mic: return "mic";
        case Chain::master: return "master";
        case Chain::count: break;
    }
    return "unknown";
}

juce::String HostGraph::chainName (Chain chain)
{
    switch (chain)
    {
        case Chain::deckA: return "Deck A Plugin Chain";
        case Chain::deckB: return "Deck B Plugin Chain";
        case Chain::deckC: return "Deck C Plugin Chain";
        case Chain::deckD: return "Deck D Plugin Chain";
        case Chain::mic: return "Mic/Vocal Plugin Chain";
        case Chain::master: return "Master Plugin Chain";
        case Chain::count: break;
    }
    return "Plugin Chain";
}

HostGraph::ChainState& HostGraph::stateFor (Chain chain)
{
    return chains[(size_t) juce::jlimit (0, (int) Chain::count - 1, (int) chain)];
}

const HostGraph::ChainState& HostGraph::stateFor (Chain chain) const
{
    return chains[(size_t) juce::jlimit (0, (int) Chain::count - 1, (int) chain)];
}

void HostGraph::initialiseGraph (ChainState& state)
{
    const juce::ScopedLock lock (state.graphLock);
    state.graph.clear();
    state.pluginNodes.clear();
    state.inputNode = state.graph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>
                                           (juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    state.outputNode = state.graph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>
                                            (juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    state.graph.setPlayConfigDetails (currentChannels, currentChannels, currentSampleRate, currentBlockSize);

    if (state.inputNode != nullptr && state.outputNode != nullptr)
        for (int ch = 0; ch < currentChannels; ++ch)
            state.graph.addConnection ({ { state.inputNode->nodeID, ch }, { state.outputNode->nodeID, ch } });

    state.graph.prepareToPlay (currentSampleRate, currentBlockSize);
    state.prepared = true;
}

void HostGraph::prepare (double sampleRate, int blockSize, int channels)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentBlockSize = juce::jmax (1, blockSize);
    currentChannels = juce::jlimit (1, 8, channels);

    for (auto& chain : chains)
        initialiseGraph (chain);
}

void HostGraph::release()
{
    for (auto& chain : chains)
    {
        chain.graph.releaseResources();
        chain.graph.clear();
        chain.pluginNodes.clear();
        chain.inputNode = nullptr;
        chain.outputNode = nullptr;
        chain.prepared = false;
    }
}

void HostGraph::processChain (Chain chain, juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    auto& state = stateFor (chain);
    if (! state.prepared || state.pluginNodes.isEmpty())
        return;

    const juce::ScopedTryLock lock (state.graphLock);
    if (! lock.isLocked())
        return;

    state.graph.processBlock (audio, midi);
}

juce::StringArray HostGraph::getSupportedPluginFormats() const
{
    juce::StringArray names;
    for (auto* format : formatManager.getFormats())
        if (format != nullptr)
            names.addIfNotAlreadyThere (format->getName());
    return names;
}

juce::String HostGraph::getStatusSummary() const
{
    auto formats = getSupportedPluginFormats();
    juce::String text = standaloneHostingEnabled()
        ? "Standalone HostGraph ready"
        : "HostGraph compiled headless for plugin builds; audio hosting is intended for Standalone";

    text += formats.isEmpty() ? "; no enabled plugin formats" : "; formats: " + formats.joinIntoString (", ");
    text += "; cached plugins: " + juce::String (knownPluginList.getNumTypes());

    int loaded = 0;
    for (const auto& chain : chains)
        loaded += chain.pluginNodes.size();
    text += "; loaded chain plugins: " + juce::String (loaded);
    return text;
}

juce::Array<juce::PluginDescription> HostGraph::getKnownPlugins() const
{
    return knownPluginList.getTypes();
}

juce::File HostGraph::getCacheFile() const
{
    return hostDataDirectory().getChildFile ("known-plugins.xml");
}

bool HostGraph::loadCache()
{
    auto cache = getCacheFile();
    if (! cache.existsAsFile())
        return false;

    std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (cache));
    if (! xml)
        return false;

    knownPluginList.recreateFromXml (*xml);
    return true;
}

bool HostGraph::saveCache() const
{
    if (auto xml = knownPluginList.createXml())
        return xml->writeTo (getCacheFile());
    return false;
}

juce::AudioPluginFormat* HostGraph::findFormatByName (const juce::String& formatName) const
{
    for (auto* format : formatManager.getFormats())
        if (format != nullptr && format->getName().equalsIgnoreCase (formatName))
            return format;
    return nullptr;
}

juce::File HostGraph::getDeadMansPedalFileForFormat (const juce::String& formatName) const
{
    auto safeName = formatName.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_ ")
                              .trim().replaceCharacter (' ', '-');
    return hostDataDirectory().getChildFile (safeName + "-dead-mans-pedal.txt");
}

HostGraph::ScanResult HostGraph::scanDefaultLocations (int maxPluginsPerFormat, bool recursive)
{
    ScanResult result;

    for (auto* format : formatManager.getFormats())
    {
        if (format == nullptr)
            continue;

        const auto formatName = format->getName();
        result.scannedFormats.add (formatName);

        if (! format->canScanForPlugins())
        {
            result.messages.add (formatName + ": no directory scanner exposed.");
            continue;
        }

        auto searchPath = format->getDefaultLocationsToSearch();
        if (searchPath.getNumPaths() == 0)
        {
            result.messages.add (formatName + ": no default search path.");
            continue;
        }

        juce::PluginDirectoryScanner scanner (knownPluginList,
                                              *format,
                                              searchPath,
                                              recursive,
                                              getDeadMansPedalFileForFormat (formatName),
                                              true);

        juce::String currentlyScanning;
        int scannedCount = 0;
        while (scannedCount < maxPluginsPerFormat && scanner.scanNextFile (true, currentlyScanning))
            ++scannedCount;

        result.blacklistedFiles.addArray (knownPluginList.getBlacklistedFiles());
        result.messages.add (formatName + ": scanned " + juce::String (scannedCount)
                             + " candidate" + (scannedCount == 1 ? "" : "s")
                             + "; next: " + scanner.getNextPluginFileThatWillBeScanned());
    }

    saveCache();
    result.plugins = knownPluginList.getTypes();
    const auto sampleNames = pluginNamesForLog (result.plugins, 8);
    if (! sampleNames.isEmpty())
        result.messages.add ("Known plugin sample: " + sampleNames.joinIntoString (", "));
    return result;
}

void HostGraph::loadPluginToChainAsync (Chain chain,
                                        const juce::PluginDescription& description,
                                        LoadCallback callback)
{
    const auto target = chain;
    formatManager.createPluginInstanceAsync (description, currentSampleRate, currentBlockSize,
        [this, target, completion = std::move (callback), requestedName = description.name]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) mutable
        {
            if (instance == nullptr)
            {
                if (completion)
                    completion (false, error.isNotEmpty() ? error : "Could not instantiate " + requestedName);
                return;
            }

            auto& state = stateFor (target);
            if (! state.prepared)
                initialiseGraph (state);

            const juce::ScopedLock lock (state.graphLock);
            state.graph.clear();
            state.pluginNodes.clear();
            state.inputNode = state.graph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>
                                                   (juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
            state.outputNode = state.graph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>
                                                    (juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
            auto pluginNode = state.graph.addNode (std::move (instance));
            if (state.inputNode != nullptr && state.outputNode != nullptr && pluginNode != nullptr)
            {
                for (int ch = 0; ch < currentChannels; ++ch)
                {
                    state.graph.addConnection ({ { state.inputNode->nodeID, ch }, { pluginNode->nodeID, ch } });
                    state.graph.addConnection ({ { pluginNode->nodeID, ch }, { state.outputNode->nodeID, ch } });
                }
                state.pluginNodes.add (pluginNode);
            }
            state.graph.setPlayConfigDetails (currentChannels, currentChannels, currentSampleRate, currentBlockSize);
            state.graph.prepareToPlay (currentSampleRate, currentBlockSize);
            state.prepared = true;

            if (completion)
                completion (true, "Loaded " + requestedName + " into " + chainName (target));
        });
}

void HostGraph::clearChain (Chain chain)
{
    initialiseGraph (stateFor (chain));
}
