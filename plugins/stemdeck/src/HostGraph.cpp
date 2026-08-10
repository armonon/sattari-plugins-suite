#include "HostGraph.h"

namespace
{
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
static bool isTrustedSattariDescription (
    const juce::PluginDescription& description)
{
    return isSattariPluginFile (description.name)
        || isSattariPluginFile (
            juce::File (description.fileOrIdentifier)
                .getFileNameWithoutExtension());
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

    // Instantiation can fail more violently than scanning. Arm a second dead-man's
    // pedal around every async load. If the app vanished before its callback, the
    // exact plugin is quarantined on the next launch instead of creating a crash loop.
    const auto quarantine = getLoadQuarantineFile();
    if (quarantine.existsAsFile())
        quarantinedPluginFiles.addLines (
            quarantine.loadFileAsString());
    const auto loadPedal = getLoadDeadMansPedalFile();
    if (loadPedal.existsAsFile())
    {
        const auto crashed = loadPedal.loadFileAsString().trim();
        if (crashed.isNotEmpty())
        {
            quarantinedPluginFiles.addIfNotAlreadyThere (crashed);
            quarantine.replaceWithText (
                quarantinedPluginFiles.joinIntoString ("\n"));
        }
        loadPedal.deleteFile();
    }

    // The cache may contain third-party descriptions; they are identities only and
    // are instantiated exclusively by PluginSandboxClient. Never list ourselves.
    for (const auto& d : knownPluginList.getTypes())
        if (d.name.containsIgnoreCase ("StemDeck") || isPluginQuarantined (d))
            knownPluginList.removeType (d);
}

HostGraph::~HostGraph()
{
    aliveFlag->store (false, std::memory_order_release);
    getLoadDeadMansPedalFile().deleteFile();
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
    if ((int) chain >= (int) Chain::channelExtraFirst
        && (int) chain < (int) Chain::count)
        return "channel" + juce::String (
            (int) chain - (int) Chain::channelExtraFirst + 5);
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
    if ((int) chain >= (int) Chain::channelExtraFirst
        && (int) chain < (int) Chain::count)
        return "Channel " + juce::String::charToString (
            (juce_wchar) ('E' + (int) chain
                          - (int) Chain::channelExtraFirst))
            + " Plugin Chain";
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
    state.safetyBuffer.setSize (currentChannels, currentBlockSize,
                                false, true, false);
    state.outputFaulted.store (false, std::memory_order_relaxed);
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
        if (! chain.prepared) { initialiseGraph (chain); chain.live = true; }

        // Already built: KEEP the loaded plugins and just re-prepare them at the new
        // rate/size. Re-initialising here would destroy every loaded FX (and dangle
        // any open plugin editor) on each device / sample-rate change.
        else
        {
            const juce::ScopedLock lock (chain.graphLock);
            chain.graph.setPlayConfigDetails (currentChannels, currentChannels, currentSampleRate, currentBlockSize);
            chain.graph.prepareToPlay (currentSampleRate, currentBlockSize);
            chain.safetyBuffer.setSize (currentChannels, currentBlockSize,
                                        false, true, false);
            chain.outputFaulted.store (false, std::memory_order_relaxed);
            chain.live = true;
        }
        {
            const juce::ScopedLock lock (chain.graphLock);
            if (chain.sandbox != nullptr)
                chain.sandbox->prepare (currentSampleRate, currentBlockSize,
                                        currentChannels);
        }
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

    // Sandbox lifetime is protected by graphLock too. The audio callback still
    // never waits: a concurrent load/remove simply dry-bypasses this block.
    if (state.sandbox != nullptr)
    {
        state.sandbox->process (audio);
        return;
    }

    if (! state.prepared || ! state.live || state.pluginNodes.isEmpty())
        return;

    if (state.outputFaulted.load (std::memory_order_relaxed))
        return;

    // The host should honour the maximum block size supplied to prepare(), but
    // bypass safely if it does not. Without a complete dry copy we cannot recover
    // deterministically from invalid third-party output.
    if (audio.getNumChannels() > state.safetyBuffer.getNumChannels()
        || audio.getNumSamples() > state.safetyBuffer.getNumSamples())
        return;

    const int channelsToCopy = juce::jmin (audio.getNumChannels(),
                                            state.safetyBuffer.getNumChannels());
    const int samplesToCopy = juce::jmin (audio.getNumSamples(),
                                           state.safetyBuffer.getNumSamples());
    for (int channel = 0; channel < channelsToCopy; ++channel)
        state.safetyBuffer.copyFrom (channel, 0, audio, channel, 0, samplesToCopy);

    state.graph.processBlock (audio, midi);

    bool invalid = false;
    for (int channel = 0; channel < audio.getNumChannels() && ! invalid; ++channel)
    {
        const auto* samples = audio.getReadPointer (channel);
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            if (! std::isfinite (samples[sample]) || std::abs (samples[sample]) > 32.0f)
            {
                invalid = true;
                break;
            }
    }
    if (invalid)
    {
        for (int channel = 0; channel < channelsToCopy; ++channel)
            audio.copyFrom (channel, 0, state.safetyBuffer, channel, 0, samplesToCopy);
        state.outputFaulted.store (true, std::memory_order_release);
    }
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
    if (! quarantinedPluginFiles.isEmpty())
        text += "; quarantined: "
             + juce::String (quarantinedPluginFiles.size());

    int loaded = 0;
    int faulted = 0;
    for (const auto& chain : chains)
    {
        loaded += chain.pluginCountAtomic.load (std::memory_order_relaxed);
        faulted += chain.outputFaulted.load (std::memory_order_relaxed) ? 1 : 0;
        const juce::ScopedTryLock lock (chain.graphLock);
        if (lock.isLocked() && chain.sandbox != nullptr
            && chain.sandbox->hasCrashed()) ++faulted;
    }
    text += "; loaded chain plugins: " + juce::String (loaded);
    if (faulted > 0)
        text += "; safety-bypassed chains: " + juce::String (faulted);
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

juce::File HostGraph::getLoadDeadMansPedalFile() const
{
    return hostDataDirectory().getChildFile ("load_pedal.txt");
}

juce::File HostGraph::getLoadQuarantineFile() const
{
    return hostDataDirectory().getChildFile ("load_quarantine.txt");
}

bool HostGraph::isPluginQuarantined (
    const juce::PluginDescription& description) const
{
    return quarantinedPluginFiles.contains (
        description.fileOrIdentifier);
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

juce::ValueTree HostGraph::createState() const
{
    juce::ValueTree root ("HOST_GRAPH");
    root.setProperty ("version", 1, nullptr);
    for (int chainIndex = 0; chainIndex < (int) Chain::count; ++chainIndex)
    {
        const auto chain = (Chain) chainIndex;
        const auto& state = stateFor (chain);
        const juce::ScopedLock lock (state.graphLock);
        for (int insert = 0; insert < state.pluginNodes.size(); ++insert)
        {
            const auto node = state.pluginNodes.getReference (insert);
            if (node == nullptr) continue;
            auto* instance = dynamic_cast<juce::AudioPluginInstance*> (
                node->getProcessor());
            if (instance == nullptr) continue;

            juce::PluginDescription description;
            instance->fillInPluginDescription (description);
            auto xml = description.createXml();
            if (xml == nullptr) continue;

            juce::MemoryBlock pluginState;
            instance->getStateInformation (pluginState);
            juce::ValueTree item ("INSERT");
            item.setProperty ("chain", chainIndex, nullptr);
            item.setProperty ("order", insert, nullptr);
            item.setProperty ("description", xml->toString(), nullptr);
            item.setProperty (
                "state", pluginState.toBase64Encoding(), nullptr);
            item.setProperty (
                "bypassed", node->isBypassed(), nullptr);
            root.appendChild (item, nullptr);
        }
        if (state.sandbox != nullptr)
            if (auto xml = state.sandboxDescription.createXml())
            {
                juce::ValueTree item ("INSERT");
                item.setProperty ("chain", chainIndex, nullptr);
                item.setProperty ("order", 0, nullptr);
                item.setProperty ("description", xml->toString(), nullptr);
                item.setProperty ("sandboxed", true, nullptr);
                root.appendChild (item, nullptr);
            }
    }
    return root;
}

juce::StringArray HostGraph::restoreState (
    const juce::ValueTree& root)
{
    juce::StringArray messages;
    for (int chain = 0; chain < (int) Chain::count; ++chain)
        initialiseGraph (stateFor ((Chain) chain));

    if (! root.isValid() || root.getType() != juce::Identifier ("HOST_GRAPH"))
        return messages;

    for (int child = 0; child < root.getNumChildren(); ++child)
    {
        const auto item = root.getChild (child);
        if (item.getType() != juce::Identifier ("INSERT"))
            continue;
        const int chainIndex = juce::jlimit (
            0, (int) Chain::count - 1,
            (int) item.getProperty ("chain", 0));
        const auto descriptionXml =
            item.getProperty ("description").toString();
        auto xml = juce::XmlDocument::parse (descriptionXml);
        juce::PluginDescription description;
        if (xml == nullptr || ! description.loadFromXml (*xml))
        {
            messages.add ("Skipped an insert with invalid plugin identity.");
            continue;
        }
        if (isPluginQuarantined (description))
        {
            messages.add (
                "Skipped unavailable/quarantined insert: "
                + description.name);
            continue;
        }

        if (! isTrustedSattariDescription (description))
        {
            auto& chainState = stateFor ((Chain) chainIndex);
            const juce::ScopedLock lock (chainState.graphLock);
            chainState.sandbox = std::make_unique<PluginSandboxClient>();
            chainState.sandbox->prepare (currentSampleRate, currentBlockSize,
                                         currentChannels);
            chainState.sandboxDescription = description;
            chainState.pluginCountAtomic.store (1, std::memory_order_relaxed);
            chainState.sandbox->launch (description, {});
            messages.add ("Restoring " + description.name
                          + " in isolated helper process");
            continue;
        }

        const auto pedal = getLoadDeadMansPedalFile();
        pedal.replaceWithText (description.fileOrIdentifier);
        juce::String error;
        auto instance = formatManager.createPluginInstance (
            description, currentSampleRate, currentBlockSize, error);
        if (instance == nullptr)
        {
            pedal.deleteFile();
            messages.add (
                "Could not restore " + description.name + ": " + error);
            continue;
        }

        juce::MemoryBlock pluginState;
        if (pluginState.fromBase64Encoding (
                item.getProperty ("state").toString())
            && pluginState.getSize() > 0)
            instance->setStateInformation (
                pluginState.getData(), (int) pluginState.getSize());

        auto& chainState = stateFor ((Chain) chainIndex);
        const juce::ScopedLock lock (chainState.graphLock);
        if (auto node = chainState.graph.addNode (std::move (instance)))
        {
            node->setBypassed ((bool) item.getProperty (
                "bypassed", false));
            chainState.pluginNodes.add (node);
            chainState.pluginCountAtomic.store (
                chainState.pluginNodes.size(),
                std::memory_order_relaxed);
            rebuildChainConnections (chainState);
            chainState.graph.setPlayConfigDetails (
                currentChannels, currentChannels,
                currentSampleRate, currentBlockSize);
            chainState.graph.prepareToPlay (
                currentSampleRate, currentBlockSize);
            chainState.prepared = true;
            chainState.live = true;
            // Disarm only after state restoration and graph preparation both
            // survived. Those phases execute plugin code too.
            pedal.deleteFile();
            messages.add (
                "Restored " + description.name + " to "
                    + chainName ((Chain) chainIndex));
        }
        else
        {
            pedal.deleteFile();
            messages.add (
                "Could not attach " + description.name
                    + " to the audio graph.");
        }
    }
    return messages;
}

juce::AudioPluginFormat* HostGraph::findFormatByName (const juce::String& formatName) const
{
    for (auto* format : formatManager.getFormats())
        if (format != nullptr && format->getName().equalsIgnoreCase (formatName))
            return format;
    return nullptr;
}

HostGraph::ScanResult HostGraph::scanDefaultLocations (int maxPluginsPerFormat, bool recursive)
{
    ScanResult result;
    if (! PluginSandboxClient::scanInHelper (
            knownPluginList, result.messages, maxPluginsPerFormat, recursive))
        result.messages.add ("No third-party effects were returned by the sandbox scan");
    result.plugins = knownPluginList.getTypes();
    result.scannedFormats = getSupportedPluginFormats();
    result.blacklistedFiles = knownPluginList.getBlacklistedFiles();
    saveCache();
    return result;
}

void HostGraph::loadPluginToChainAsync (Chain chain,
                                        const juce::PluginDescription& description,
                                        LoadCallback callback)
{
    if (! isTrustedSattariDescription (description))
    {
        auto& state = stateFor (chain);
        const juce::ScopedLock lock (state.graphLock);
        if (! state.pluginNodes.isEmpty() || state.sandbox != nullptr)
        {
            if (callback) callback (false,
                "Use an empty chain for an isolated third-party effect");
            return;
        }
        state.sandbox = std::make_unique<PluginSandboxClient>();
        state.sandbox->prepare (currentSampleRate, currentBlockSize,
                                currentChannels);
        state.sandboxDescription = description;
        state.sandbox->launch (description,
            [this, target = chain, alive = aliveFlag,
             completion = std::move (callback)]
            (bool ok, juce::String message) mutable
            {
                if (! alive->load (std::memory_order_acquire)) return;
                auto& targetState = stateFor (target);
                const juce::ScopedLock targetLock (targetState.graphLock);
                targetState.pluginCountAtomic.store (ok ? 1 : 0,
                                                     std::memory_order_release);
                if (! ok) targetState.sandbox.reset();
                if (completion) completion (ok, message);
            });
        return;
    }
    if (isPluginQuarantined (description))
    {
        if (callback)
            callback (
                false, description.name
                    + " is quarantined because it crashed during a previous load.");
        return;
    }
    if (pluginLoadInFlight.exchange (true, std::memory_order_acq_rel))
    {
        if (callback)
            callback (false, "Another plugin is still loading. Try again shortly.");
        return;
    }

    const auto loadPedal = getLoadDeadMansPedalFile();
    loadPedal.replaceWithText (description.fileOrIdentifier);
    const auto target = chain;
    const auto alive = aliveFlag;
    formatManager.createPluginInstanceAsync (description, currentSampleRate, currentBlockSize,
        [this, target, completion = std::move (callback),
         requestedName = description.name, loadPedal, alive]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) mutable
        {
            if (! alive->load (std::memory_order_acquire))
                return;
            pluginLoadInFlight.store (false, std::memory_order_release);
            if (instance == nullptr)
            {
                loadPedal.deleteFile();
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
            auto pluginNode = state.graph.addNode (std::move (instance));
            if (pluginNode != nullptr)
            {
                state.pluginNodes.add (pluginNode);
                state.pluginCountAtomic.store (state.pluginNodes.size(), std::memory_order_relaxed);
            }
            else
            {
                loadPedal.deleteFile();
                if (completion)
                    completion (false, "Could not attach "
                                       + requestedName
                                       + " to the audio graph.");
                return;
            }
            rebuildChainConnections (state);
            state.graph.setPlayConfigDetails (currentChannels, currentChannels, currentSampleRate, currentBlockSize);
            state.graph.prepareToPlay (currentSampleRate, currentBlockSize);
            state.prepared = true;
            // prepareToPlay can execute arbitrary plugin code; keep the pedal
            // armed until the complete load path has survived.
            loadPedal.deleteFile();

            if (completion)
                completion (true, "Loaded " + requestedName + " into " + chainName (target));
        });
}

void HostGraph::clearChain (Chain chain)
{
    auto& state = stateFor (chain);
    {
        const juce::ScopedLock lock (state.graphLock);
        if (state.sandbox != nullptr) state.sandbox->stop();
        state.sandbox.reset();
        state.sandboxDescription = {};
    }
    initialiseGraph (state);
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
    if (state.sandbox != nullptr)
        return state.sandbox->latencySamples();
    if (! state.prepared || state.pluginNodes.isEmpty())
        return 0;
    // Serial chain: the graph reports the summed latency of its path.
    return juce::jmax (0, state.graph.getLatencySamples());
}

int HostGraph::getChainPluginCount (Chain chain) const
{
    return stateFor (chain).pluginCountAtomic.load (std::memory_order_acquire);
}

juce::String HostGraph::getChainPluginName (Chain chain, int index) const
{
    const auto& state = stateFor (chain);
    const juce::ScopedLock lock (state.graphLock);
    if (state.sandbox != nullptr)
        return index == 0 ? state.sandbox->pluginName() + " (Isolated)"
                          : juce::String();
    if (! juce::isPositiveAndBelow (index, state.pluginNodes.size())) return {};
    if (auto* proc = state.pluginNodes[index]->getProcessor()) return proc->getName();
    return {};
}

juce::AudioProcessor* HostGraph::getChainPluginProcessor (Chain chain, int index) const
{
    const auto& state = stateFor (chain);
    const juce::ScopedLock lock (state.graphLock);
    if (state.sandbox != nullptr) return nullptr;
    if (! juce::isPositiveAndBelow (index, state.pluginNodes.size())) return nullptr;
    return state.pluginNodes[index]->getProcessor();
}

void HostGraph::moveChainPlugin (Chain chain, int fromIndex, int toIndex)
{
    auto& state = stateFor (chain);
    const juce::ScopedLock lock (state.graphLock);
    if (state.sandbox != nullptr) return;
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
    if (state.sandbox != nullptr) return;
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
    if (state.sandbox != nullptr) return false;
    if (! juce::isPositiveAndBelow (index, state.pluginNodes.size())) return false;
    auto* node = state.pluginNodes[index].get();
    return node != nullptr && node->isBypassed();
}

std::uint64_t HostGraph::stableParameterId (juce::AudioProcessorParameter& parameter,
                                            int fallbackParameterIndex)
{
    juce::String identity;
    if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (&parameter))
        identity = "id:" + withId->paramID;
    if (identity.isEmpty())
        identity = "fallback:" + juce::String (fallbackParameterIndex)
                 + ":" + parameter.getName (256);
    return (std::uint64_t) identity.hashCode64();
}

std::vector<HostGraph::ParameterSnapshot> HostGraph::getParameterSnapshots() const
{
    std::vector<ParameterSnapshot> result;
    for (int chainIndex = 0; chainIndex < (int) Chain::count; ++chainIndex)
    {
        const auto chain = (Chain) chainIndex;
        const auto& state = stateFor (chain);
        const juce::ScopedTryLock lock (state.graphLock);
        if (! lock.isLocked())
            continue;
        for (int insert = 0; insert < state.pluginNodes.size(); ++insert)
        {
            auto* processor = state.pluginNodes[insert] != nullptr
                                ? state.pluginNodes[insert]->getProcessor() : nullptr;
            if (processor == nullptr)
                continue;
            const auto& parameters = processor->getParameters();
            for (int parameterIndex = 0; parameterIndex < parameters.size(); ++parameterIndex)
            {
                auto* parameter = parameters[parameterIndex];
                if (parameter == nullptr)
                    continue;
                result.push_back ({
                    chainIndex, insert, parameterIndex,
                    stableParameterId (*parameter, parameterIndex),
                    parameter->getValue()
                });
            }
        }
    }
    return result;
}

bool HostGraph::setPluginParameterByStableId (
    Chain chain, int insertIndex, std::uint64_t wantedStableId,
    int fallbackParameterIndex, float normalizedValue)
{
    auto& state = stateFor (chain);
    const juce::ScopedLock lock (state.graphLock);
    if (! juce::isPositiveAndBelow (insertIndex, state.pluginNodes.size()))
        return false;
    auto* processor = state.pluginNodes[insertIndex] != nullptr
                        ? state.pluginNodes[insertIndex]->getProcessor() : nullptr;
    if (processor == nullptr)
        return false;
    const auto& parameters = processor->getParameters();
    juce::AudioProcessorParameter* match = nullptr;
    for (int index = 0; index < parameters.size(); ++index)
        if (parameters[index] != nullptr
            && stableParameterId (*parameters[index], index) == wantedStableId)
        {
            match = parameters[index];
            break;
        }
    if (match == nullptr && juce::isPositiveAndBelow (
            fallbackParameterIndex, parameters.size()))
        match = parameters[fallbackParameterIndex];
    if (match == nullptr)
        return false;
    match->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalizedValue));
    return true;
}

void HostGraph::removeChainPlugin (Chain chain, int index)
{
    auto& state = stateFor (chain);
    const juce::ScopedLock lock (state.graphLock);
    if (state.sandbox != nullptr)
    {
        if (index == 0)
        {
            state.sandbox->stop();
            state.sandbox.reset();
            state.sandboxDescription = {};
            state.pluginCountAtomic.store (0, std::memory_order_release);
        }
        return;
    }
    if (! juce::isPositiveAndBelow (index, state.pluginNodes.size())) return;
    state.graph.removeNode (state.pluginNodes[index]->nodeID);
    state.pluginNodes.remove (index);
    state.pluginCountAtomic.store (state.pluginNodes.size(), std::memory_order_relaxed);
    rebuildChainConnections (state);
    state.graph.prepareToPlay (currentSampleRate, currentBlockSize);
}
