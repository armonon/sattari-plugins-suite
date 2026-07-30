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
        // 16 per-stem chains (4 decks x 4 stems), deck-major. Appended AFTER master so
        // the six original ids keep their numeric values — chainId() strings are
        // persisted in saved state, and renumbering would silently reassign a user's
        // FX to a different chain on load.
        stemFirst,
        // Appended after the original persisted chain ids and the 16 stem chains.
        // These give dynamic channel slots E-H real insert racks without renumbering
        // any existing session's chain.
        channelExtraFirst = stemFirst + 16,
        count = channelExtraFirst + 4
    };

    static constexpr int kNumDecks       = 4;
    static constexpr int kNumChannelChains = 8;
    static constexpr int kNumStemsPerDeck = 4;   // drums, bass, music, vocals

    // stemIndex is 0..3 (drums, bass, music, vocals) — i.e. DeckPlayer::drumsStem - 1.
    static constexpr Chain stemChain (int deckIndex, int stemIndex) noexcept
    {
        return (Chain) ((int) Chain::stemFirst + deckIndex * kNumStemsPerDeck + stemIndex);
    }
    static constexpr bool isStemChain (Chain c) noexcept
    {
        return (int) c >= (int) Chain::stemFirst
            && (int) c < (int) Chain::channelExtraFirst;
    }
    static constexpr Chain channelChain (int slot) noexcept
    {
        return slot < kNumDecks
                   ? (Chain) slot
                   : (Chain) ((int) Chain::channelExtraFirst
                              + slot - kNumDecks);
    }
    static constexpr bool isChannelChain (Chain c) noexcept
    {
        return ((int) c >= 0 && (int) c < kNumDecks)
            || ((int) c >= (int) Chain::channelExtraFirst
                && (int) c < (int) Chain::count);
    }

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
    // Scan ONLY the Sattari suite (safe: third-party plugins can crash the host mid-scan).
    ScanResult scanSattariOnly();
    bool loadCache();
    bool saveCache() const;

    void loadPluginToChainAsync (Chain chain,
                                 const juce::PluginDescription& description,
                                 LoadCallback callback);
    void clearChain (Chain chain);

    // Rack introspection (message thread): list loaded plugins + reach their editors.
    int getChainPluginCount (Chain chain) const;
    // Latency this chain's loaded plugins introduce, in samples. Drives plugin delay
    // compensation so channels with FX stay phase-aligned with those without (§3.3).
    int getChainLatencySamples (Chain chain) const;
    juce::String getChainPluginName (Chain chain, int index) const;
    juce::AudioProcessor* getChainPluginProcessor (Chain chain, int index) const;
    void removeChainPlugin (Chain chain, int index);
    // Reorder and bypass: pluginNodes is an ordered array that rebuildChainConnections
    // wires input->[0]->[1]->...->output, so moving an entry IS reordering the chain.
    void moveChainPlugin (Chain chain, int fromIndex, int toIndex);
    void setChainPluginBypassed (Chain chain, int index, bool bypassed);
    bool isChainPluginBypassed (Chain chain, int index) const;

    struct ParameterSnapshot
    {
        int chainIndex = 0;
        int insertIndex = 0;
        int fallbackParameterIndex = 0;
        std::uint64_t stableParameterId = 0;
        float normalizedValue = 0.0f;
    };
    // Message-thread polling bridge for native hosted-plugin editors. Parameter
    // callbacks may run on the audio thread, so capture observes normalized values
    // here instead of taking locks or allocating in AudioProcessorParameter::Listener.
    std::vector<ParameterSnapshot> getParameterSnapshots() const;
    bool setPluginParameterByStableId (Chain chain, int insertIndex,
                                       std::uint64_t stableParameterId,
                                       int fallbackParameterIndex,
                                       float normalizedValue);

    static juce::String chainId (Chain chain);
    static juce::String chainName (Chain chain);
    static std::uint64_t stableParameterId (juce::AudioProcessorParameter&,
                                            int fallbackParameterIndex);

private:
    struct ChainState
    {
        juce::AudioProcessorGraph graph;
        juce::AudioProcessorGraph::Node::Ptr inputNode;
        juce::AudioProcessorGraph::Node::Ptr outputNode;
        juce::Array<juce::AudioProcessorGraph::Node::Ptr> pluginNodes;
        juce::CriticalSection graphLock;
        bool prepared = false;   // graph built (I/O nodes exist); plugins may be loaded
        bool live = false;       // prepareToPlay done — safe for processChain
        // Mirrors pluginNodes.size() for a lock-free "is this chain empty?" check on
        // the audio thread. Written on the message thread under graphLock.
        std::atomic<int> pluginCountAtomic { 0 };
    };

    juce::AudioPluginFormat* findFormatByName (const juce::String& formatName) const;
    juce::File getDeadMansPedalFileForFormat (const juce::String& formatName) const;
    ChainState& stateFor (Chain chain);
    const ChainState& stateFor (Chain chain) const;
    void initialiseGraph (ChainState& state);
    void rebuildChainConnections (ChainState& state);   // input -> plugins... -> output
    bool standaloneHostingEnabled() const noexcept;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    std::array<ChainState, (size_t) Chain::count> chains;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    int currentChannels = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostGraph)
};
