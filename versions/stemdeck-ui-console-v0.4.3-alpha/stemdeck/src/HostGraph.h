#pragma once

#include <JuceHeader.h>
#include <array>

// Standalone-first plugin host graph. The graph is intentionally separate from
// DeckPlayer so plugin scanning/instantiation can happen off the audio path and
// processBlock only sees already-prepared AudioProcessorGraph chains.
class HostGraph
{
public:
    enum class Chain
    {
        deckA = 0,
        deckB,
        deckC,
        deckD,
        mic,
        master,
        count
    };

    struct ScanResult
    {
        juce::StringArray scannedFormats;
        juce::Array<juce::PluginDescription> plugins;
        juce::StringArray blacklistedFiles;
        juce::StringArray messages;
    };

    using LoadCallback = std::function<void(bool success, juce::String message)>;

    HostGraph();
    ~HostGraph();

    void prepare (double sampleRate, int blockSize, int channels);
    void release();
    void processChain (Chain chain, juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi);

    juce::StringArray getSupportedPluginFormats() const;
    juce::String getStatusSummary() const;
    juce::Array<juce::PluginDescription> getKnownPlugins() const;
    juce::File getCacheFile() const;

    ScanResult scanDefaultLocations (int maxPluginsPerFormat = 64, bool recursive = true);
    bool loadCache();
    bool saveCache() const;

    void loadPluginToChainAsync (Chain chain,
                                 const juce::PluginDescription& description,
                                 LoadCallback callback);
    void clearChain (Chain chain);

    static juce::String chainId (Chain chain);
    static juce::String chainName (Chain chain);

private:
    struct ChainState
    {
        juce::AudioProcessorGraph graph;
        juce::AudioProcessorGraph::Node::Ptr inputNode;
        juce::AudioProcessorGraph::Node::Ptr outputNode;
        juce::Array<juce::AudioProcessorGraph::Node::Ptr> pluginNodes;
        juce::CriticalSection graphLock;
        bool prepared = false;
    };

    juce::AudioPluginFormat* findFormatByName (const juce::String& formatName) const;
    juce::File getDeadMansPedalFileForFormat (const juce::String& formatName) const;
    ChainState& stateFor (Chain chain);
    const ChainState& stateFor (Chain chain) const;
    void initialiseGraph (ChainState& state);
    bool standaloneHostingEnabled() const noexcept;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    std::array<ChainState, (size_t) Chain::count> chains;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    int currentChannels = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostGraph)
};
