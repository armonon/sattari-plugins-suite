#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "DeckPlayer.h"
#include "HostGraph.h"
#include "MasterRecorder.h"
#include "PadBank.h"
#include "PerformanceRecorder.h"
#include "StemProvider.h"
#include "TransportSync.h"
#include "WarpEngine.h"

class MainPluginProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int numDecks = 4;

    MainPluginProcessor();
    ~MainPluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }
    const juce::AudioProcessorValueTreeState& parameters() const { return apvts; }

    void loadDeckFileAsync (int deckIndex, const juce::File& file);
    void loadDeckStemFileAsync (int deckIndex, int stemLaneIndex, const juce::File& file);
    void requestDeckAiStemsAsync (int deckIndex);
    void loadPadFileAsync (int padIndex, const juce::File& file);
    void triggerPad (int padIndex);
    void rewindDeck (int deckIndex);
    bool beginMasterRecording (const juce::File& destinationFile);
    juce::File stopMasterRecording();
    bool isMasterRecording() const noexcept { return masterRecorder.isRecording(); }
    juce::File getMasterRecordingTarget() const { return masterRecorder.getTargetFile(); }
    juce::File getMicRecordingTarget() const { return micRecorder.getTargetFile(); }
    juce::File getLastPerformanceEventLogFile() const { return lastPerformanceEventLogFile; }
    juce::File getLastArrangementFile() const { return lastArrangementFile; }

    juce::String getDeckPath (int deckIndex) const;
    juce::String getDeckStemPath (int deckIndex, int stemLaneIndex) const;
    juce::StringArray getStemLaneLabels() const;
    juce::StringArray getKeyTargetChoices() const;
    juce::StringArray getTempoInterpretationChoices() const;
    juce::StringArray getSyncModeChoices() const;
    TrackAnalysis getDeckAnalysis (int deckIndex) const;
    TrackAnalysis getDeckLiveAnalysis (int deckIndex) const;
    TrackAnalysis getDeckActiveAnalysis (int deckIndex) const;
    void refreshLiveKeyAnalysis();
    juce::String getDeckKeyInfo (int deckIndex) const;
    juce::String getDeckTimePitchStatus (int deckIndex) const;
    juce::String getDeckBeatSyncStatus (int deckIndex) const;
    juce::String getWarpEngineStatus() const;
    juce::StringArray getSupportedPluginHostFormats() const;
    juce::String getPluginHostStatus() const;
    juce::String getStemProviderStatus() const;
    juce::String getDeckStemProviderStatus (int deckIndex) const;
    juce::Array<juce::PluginDescription> getKnownHostPlugins() const;
    juce::File getPluginHostCacheFile() const;
    bool loadPluginHostCache();
    bool savePluginHostCache() const;
    HostGraph::ScanResult scanPluginHostDefaultLocations (int maxPluginsPerFormat = 32, bool recursive = true);
    juce::StringArray getPluginHostChainNames() const;
    void loadHostPluginToChainAsync (int chainIndex, int pluginIndex, HostGraph::LoadCallback callback);
    void clearHostPluginChain (int chainIndex);
    juce::String getPadPath (int padIndex) const;
    juce::StringArray getSupportedAudioExtensions() const;
    TransportSync::Snapshot getTransportSnapshot() const { return sync.getSnapshot(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static juce::String deckPrefix (int deckIndex);
    static juce::String deckLabel (int deckIndex);
    static juce::String deckPathProperty (int deckIndex);
    static juce::String deckStemPathProperty (int deckIndex, int stemLaneIndex);
    static juce::String padPathProperty (int padIndex);
    static juce::StringArray keyTargetChoices();
    static int keyTargetChoiceToPitchClass (int choiceIndex);

    void loadAudioAsync (const juce::File& file, std::function<void(std::shared_ptr<const DeckPlayer::LoadedAudio>)> onLoaded);
    std::shared_ptr<const DeckPlayer::LoadedAudio> loadAudioFile (const juce::File& file);
    void restoreFileAssignmentsFromState();
    void setStatePathProperty (const juce::Identifier& property, const juce::String& path);
    void applyPremiumStemMix (int deckIndex);
    float getFloatParam (const juce::String& id) const;
    bool getBoolParam (const juce::String& id) const;
    TrackAnalysis getDeckLiveAnalysisFromAtomics (int deckIndex) const;
    void applyLimiter (juce::AudioBuffer<float>& buffer, float ceilingDb);
    void initialisePerformanceCaptureState();
    void updatePerformanceCaptureState (int64_t blockStartSample, const TransportSync::Snapshot& snapshot);
    void logPerformanceEvent (int64_t sample, const juce::String& type, const juce::String& deck = {},
                              const juce::String& parameter = {}, const juce::String& value = {},
                              const juce::String& path = {}, const juce::String& details = {});

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioFormatManager formatManager;
    juce::ThreadPool loadPool { 4 };

    std::array<DeckPlayer, numDecks> decks;
    PadBank padBank;
    MasterRecorder masterRecorder;
    MasterRecorder micRecorder;
    std::array<MasterRecorder, numDecks> deckRecorders;
    PerformanceRecorder performanceRecorder;
    TransportSync sync;
    WarpEngine warpEngine;
    HostGraph hostGraph;
    StemProvider stemProvider;

    double currentSampleRate = 44100.0;
    float limiterEnvelope = 1.0f;
    std::array<std::atomic<int>, numDecks> liveKeyPitchClass;
    std::array<std::atomic<int>, numDecks> liveKeyIsMinor;
    std::array<std::atomic<float>, numDecks> liveKeyConfidence;
    std::array<int64_t, numDecks> lastLiveKeyAnalysisMs {};
    std::atomic<int64_t> recordingSampleCounter { 0 };
    int64_t transportSampleCounter = 0;
    juce::File lastPerformanceEventLogFile;
    juce::File lastArrangementFile;
    std::array<juce::File, numDecks> lastDeckRecordingTargets;
    std::array<juce::AudioBuffer<float>, numDecks> deckScratchBuffers;

    struct DeckPerformanceState
    {
        bool play = false;
        bool keyLock = false;
        bool liveKey = false;
        float bpm = 120.0f;
        float volume = 0.0f;
        float fader = 1.0f;
        float pitch = 0.0f;
        float beatOffset = 0.0f;
        float downbeat = 0.0f;
        float low = 0.0f;
        float mid = 0.0f;
        float high = 0.0f;
        float filter = 0.0f;
        int tempoInterpretation = 0;
        int syncMode = 0;
    };

    std::array<DeckPerformanceState, numDecks> lastDeckPerformanceState;
    bool lastMicMonitor = false;
    bool lastHostSync = true;
    bool lastBeatSync = false;
    float lastCrossfader = 0.5f;
    float lastMasterBpm = 120.0f;
    float lastMicGain = -12.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPluginProcessor)
};
