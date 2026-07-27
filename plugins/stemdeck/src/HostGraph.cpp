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

static bool isSattariPluginFile (const juce::String& fileName);   // defined below

HostGraph::HostGraph()
{
    // Current JUCE API: do not use deprecated AudioPluginFormatManager::addDefaultFormats().
#if JucePlugin_Build_Standalone
    juce::addDefaultFormatsToManager (formatManager);
#else
    juce::addHeadlessDefaultFormatsToManager (formatManager);
#endif
    loadCache();

    // Drop anything non-Sattari left in the cache from an older full scan, so a
    // third-party plugin can never be listed or instantiated (they crash the host).
    for (const auto& d : knownPluginList.getTypes())
        if (! isSattariPluginFile (d.name) && ! d.manufacturerName.containsIgnoreCase ("Sattari"))
            knownPluginList.removeType (d);
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
        default: break;
    }
    if (isStemChain (chain))
    {
        static const char* const stems[] = { "drums", "bass", "music", "vocals" };
        const int n = (int) chain - (int) Chain::stemFirst;
        return juce::String ("deck") + juce::String::charToString ((juce_wchar) ('A' + n / kNumStemsPerDeck))
             + "." + stems[n % kNumStemsPerDeck];
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
        default: break;
    }
    if (isStemChain (chain))
    {
        static const char* const stems[] = { "Drums", "Bass", "Music", "Vocals" };
        const int n = (int) chain - (int) Chain::stemFirst;
        return "Deck " + juce::String::charToString ((juce_wchar) ('A' + n / kNumStemsPerDeck))
             + " " + stems[n % kNumStemsPerDeck] + " FX";
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
    state.pluginCountAtomic.store (0, std::memory_order_relaxed);
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
    state.live = true;
}

void HostGraph::prepare (double sampleRate, int blockSize, int channels)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentBlockSize = juce::jmax (1, blockSize);
    currentChannels = juce::jlimit (1, 8, channels);

    for (auto& chain : chains)
    {
        // First time: build the I/O pass-through graph.
        if (! chain.prepared) { initialiseGraph (chain); chain.live = true; continue; }

        // Already built: KEEP the loaded plugins and just re-prepare them at the new
        // rate/size. Re-initialising here would destroy every loaded FX (and dangle
        // any open plugin editor) on each device / sample-rate change.
        const juce::ScopedLock lock (chain.graphLock);
        chain.graph.setPlayConfigDetails (currentChannels, currentChannels, currentSampleRate, currentBlockSize);
        chain.graph.prepareToPlay (currentSampleRate, currentBlockSize);
        chain.live = true;
    }
}

void HostGraph::release()
{
    for (auto& chain : chains)
    {
        // Serialise with processChain (audio thread) so it never reads prepared /
        // pluginNodes mid-teardown.
        const juce::ScopedLock lock (chain.graphLock);
        chain.graph.releaseResources();
        // Deliberately NOT clearing the graph: releaseResources() runs on every audio
        // stop (device change, settings dialog, quit), and destroying the plugin
        // processors here leaves any open plugin editor window pointing at freed
        // memory — its next paint is a use-after-free. Plugins are only destroyed on
        // explicit removal / clearChain, where the editors are closed first.
        chain.live = false;
    }
}

void HostGraph::processChain (Chain chain, juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    auto& state = stateFor (chain);

    // Fast path: with 16 stem chains added, an empty chain is the overwhelmingly common
    // case and must cost nothing. This atomic mirrors pluginNodes.size() and lets an
    // empty chain bail before touching the lock at all.
    if (state.pluginCountAtomic.load (std::memory_order_relaxed) == 0)
        return;

    // Read prepared/pluginNodes under the lock — the message thread mutates both
    // (initialiseGraph, plugin load, release) while holding graphLock.
    const juce::ScopedTryLock lock (state.graphLock);
    if (! lock.isLocked())
        return;

    if (! state.prepared || ! state.live || state.pluginNodes.isEmpty())
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
                initialiseGraph (state);   // creates fresh input/output nodes (empty pass-through)

            const juce::ScopedLock lock (state.graphLock);
            // Append the new plugin to the END of the serial chain (a rack), rather
            // than replacing — so multiple Sattari FX stack in order.
            if (auto pluginNode = state.graph.addNode (std::move (instance)))
                state.pluginNodes.add (pluginNode);
                state.pluginCountAtomic.store (state.pluginNodes.size(), std::memory_order_relaxed);
            rebuildChainConnections (state);
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

// Is this a Sattari-suite plugin file? We deliberately scan ONLY our own plugins:
// scanning arbitrary third-party VST3s in-process crashes the host (e.g. Serato
// Sample calls main-thread-only Text Input Services from the scan thread).
static bool isSattariPluginFile (const juce::String& fileName)
{
    static const char* const kAllow[] = { "AutoPitch", "Auto Pitch", "Metamorph", "Morph EQ",
                                          "Side Chain Master", "Sub Conjurer", "Vocal Prep" };
    if (fileName.containsIgnoreCase ("StemDeck")) return false;      // never host ourselves
    if (fileName.startsWithIgnoreCase ("Sattari")) return true;
    for (auto* a : kAllow) if (fileName.startsWithIgnoreCase (a)) return true;
    return false;
}

HostGraph::ScanResult HostGraph::scanSattariOnly()
{
    ScanResult result;
    auto* vst3 = findFormatByName ("VST3");
    if (vst3 == nullptr) { result.messages.add ("VST3 format unavailable"); return result; }
    result.scannedFormats.add ("VST3");

    // ── Dead man's pedal ────────────────────────────────────────────────────────
    // Scanning happens in-process, so a plugin that crashes on open takes the app
    // with it. Before opening a plugin we record its path; if that record survives
    // (i.e. we never got to clear it), that plugin crashed us last time — blacklist
    // it permanently so FX keeps working instead of dying on every launch.
    const auto dir       = getCacheFile().getParentDirectory();
    const auto pedal     = dir.getChildFile ("scan_pedal.txt");
    const auto blackFile = dir.getChildFile ("blacklist.txt");

    juce::StringArray blacklist;
    if (blackFile.existsAsFile()) blacklist.addLines (blackFile.loadFileAsString());
    if (pedal.existsAsFile())
    {
        const auto crashed = pedal.loadFileAsString().trim();
        if (crashed.isNotEmpty() && ! blacklist.contains (crashed))
        {
            blacklist.add (crashed);
            blackFile.replaceWithText (blacklist.joinIntoString ("\n"));
            result.messages.add ("Blacklisted (crashed last scan): " + juce::File (crashed).getFileName());
        }
        pedal.deleteFile();
    }

    const juce::File dirs[] = {
        juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/VST3"),
        juce::File ("/Library/Audio/Plug-Ins/VST3")
    };

    for (const auto& d : dirs)
    {
        if (! d.isDirectory()) continue;
        for (const auto& entry : juce::RangedDirectoryIterator (d, false, "*.vst3",
                                                                juce::File::findFilesAndDirectories))
        {
            const auto file = entry.getFile();
            if (! isSattariPluginFile (file.getFileNameWithoutExtension())) continue;

            const auto path = file.getFullPathName();
            if (blacklist.contains (path)) { result.blacklistedFiles.add (file.getFileName()); continue; }

            pedal.replaceWithText (path);          // arm: if we die here, it's this one
            juce::OwnedArray<juce::PluginDescription> found;
            vst3->findAllTypesForFile (found, path);
            pedal.deleteFile();                     // survived: disarm

            for (auto* pd : found)
                if (pd != nullptr && ! pd->name.containsIgnoreCase ("StemDeck"))
                {
                    knownPluginList.addType (*pd);
                    result.plugins.add (*pd);
                }
        }
    }

    result.messages.add ("Sattari-only scan: " + juce::String (result.plugins.size()) + " plugin(s)");
    saveCache();
    return result;
}

// Wire input -> plugin[0] -> plugin[1] -> ... -> output in series (empty = passthrough).
void HostGraph::rebuildChainConnections (ChainState& state)
{
    for (const auto& c : state.graph.getConnections())
        state.graph.removeConnection (c);

    juce::Array<juce::AudioProcessorGraph::Node::Ptr> nodes;
    if (state.inputNode != nullptr) nodes.add (state.inputNode);
    for (auto& p : state.pluginNodes) if (p != nullptr) nodes.add (p);
    if (state.outputNode != nullptr) nodes.add (state.outputNode);

    for (int i = 0; i + 1 < nodes.size(); ++i)
        for (int ch = 0; ch < currentChannels; ++ch)
            state.graph.addConnection ({ { nodes[i]->nodeID, ch }, { nodes[i + 1]->nodeID, ch } });
}

int HostGraph::getChainLatencySamples (Chain chain) const
{
    auto& state = stateFor (chain);
    const juce::ScopedLock lock (state.graphLock);
    if (! state.prepared || state.pluginNodes.isEmpty())
        return 0;
    // Serial chain: the graph reports the summed latency of its path.
    return juce::jmax (0, state.graph.getLatencySamples());
}

int HostGraph::getChainPluginCount (Chain chain) const
{
    return stateFor (chain).pluginNodes.size();
}

juce::String HostGraph::getChainPluginName (Chain chain, int index) const
{
    const auto& state = stateFor (chain);
    if (! juce::isPositiveAndBelow (index, state.pluginNodes.size())) return {};
    if (auto* proc = state.pluginNodes[index]->getProcessor()) return proc->getName();
    return {};
}

juce::AudioProcessor* HostGraph::getChainPluginProcessor (Chain chain, int index) const
{
    const auto& state = stateFor (chain);
    if (! juce::isPositiveAndBelow (index, state.pluginNodes.size())) return nullptr;
    return state.pluginNodes[index]->getProcessor();
}

void HostGraph::moveChainPlugin (Chain chain, int fromIndex, int toIndex)
{
    auto& state = stateFor (chain);
    const juce::ScopedLock lock (state.graphLock);
    const int n = state.pluginNodes.size();
    if (! juce::isPositiveAndBelow (fromIndex, n) || ! juce::isPositiveAndBelow (toIndex, n)
        || fromIndex == toIndex)
        return;
    state.pluginNodes.move (fromIndex, toIndex);
    rebuildChainConnections (state);   // order in the array IS the signal order
    state.graph.prepareToPlay (currentSampleRate, currentBlockSize);
}

void HostGraph::setChainPluginBypassed (Chain chain, int index, bool bypassed)
{
    auto& state = stateFor (chain);
    const juce::ScopedLock lock (state.graphLock);
    if (! juce::isPositiveAndBelow (index, state.pluginNodes.size())) return;
    // Node-level bypass: the graph keeps the connections and passes audio through, so
    // this neither reallocates nor rebuilds — safe to toggle while audio is running.
    if (auto* node = state.pluginNodes[index].get())
        node->setBypassed (bypassed);
}

bool HostGraph::isChainPluginBypassed (Chain chain, int index) const
{
    const auto& state = stateFor (chain);
    const juce::ScopedLock lock (state.graphLock);
    if (! juce::isPositiveAndBelow (index, state.pluginNodes.size())) return false;
    auto* node = state.pluginNodes[index].get();
    return node != nullptr && node->isBypassed();
}

void HostGraph::removeChainPlugin (Chain chain, int index)
{
    auto& state = stateFor (chain);
    const juce::ScopedLock lock (state.graphLock);
    if (! juce::isPositiveAndBelow (index, state.pluginNodes.size())) return;
    state.graph.removeNode (state.pluginNodes[index]->nodeID);
    state.pluginNodes.remove (index);
    state.pluginCountAtomic.store (state.pluginNodes.size(), std::memory_order_relaxed);
    rebuildChainConnections (state);
    state.graph.prepareToPlay (currentSampleRate, currentBlockSize);
}
