#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include <atomic>
#include "ArrangementPlayer.h"
#include "MidiClipPlayer.h"
#include "Metronome.h"
#include "DeckPlayer.h"
#include "HostGraph.h"
#include "MasterRecorder.h"
#include "MidiMappingManager.h"
#include "PadBank.h"
#include "PerformanceRecorder.h"
#include "StemProvider.h"
#include "RealTimeStemSeparator.h"
#include "TransportSync.h"
#include "WarpEngine.h"
#include "DeckFX.h"
#include "Channel.h"
#include "TrackDeckBody.h"
#include "InputBody.h"
#include "MidiInstrumentBody.h"
#include "MidiCapture.h"
#include "GraphSnapshot.h"
#include "MasterFx.h"
#include "SmartMaster.h"
#include "LiveLoudnessMeter.h"
#include "StemGain.h"
#include "AutomixEngine.h"
#include "NeuralKeyEngine.h"
#include "NeuralBeatEngine.h"
#include "AnalysisCache.h"
#include "EventDispatcher.h"

class MainPluginProcessor final : public juce::AudioProcessor,
                                  private juce::AudioProcessorValueTreeState::Listener
{
public:
    // Number of deck UIs / per-deck banks the app provides. Channels are dynamic
    // (add/remove at runtime); this is the CEILING for TrackDeck strips and must stay
    // in sync with PerformanceRecorder::numDecks and ArrangementBuilder::numDecks —
    // the recorder passes a fixed-size deckFiles array between them.
    static constexpr int numDecks = 8;
    // How many channels a fresh session opens with (numDecks is the CEILING, not the
    // default — opening 8 empty decks would be worse than the familiar 4).
    static constexpr int kDefaultChannels = 4;
    // Plugin-state schema. v2 changed stem gain from dB to linear percent — see
    // StemGain.h. Bump this whenever a stored value's MEANING changes, and add a
    // migration in setStateInformation; never reinterpret old values in place.
    static constexpr int kStateVersion = 2;

    // Neural key must clear this to override the DSP result (§A3 confidence gating).
    // "valid" from the model only means it ran; below this floor the measured DSP
    // answer is kept and the readout is flagged low-confidence.
    static constexpr float kNeuralKeyConfidenceFloor = 0.45f;

    MainPluginProcessor();
    ~MainPluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }   // motorized-fader feedback out
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
    void clearDeck (int deckIndex);
    void loadPadFileAsync (int padIndex, const juce::File& file);
    void triggerPad (int padIndex);
    void rewindDeck (int deckIndex);
    void seekDeckSeconds (int deckIndex, double seconds);
    void beatJumpDeck (int deckIndex, double bars);      // negative = back

    // ── Automix ──────────────────────────────────────────────────────────────
    // Plan a phrase-aligned harmonic transition from outDeck to inDeck (pure).
    stemdeck::TransitionPlan planAutomixTransition (int outDeck, int inDeck) const;
    // Execute the discrete setup: cue inDeck to the plan's in-point, assign
    // crossfader sides (out=A/left, in=B/right), enable beat sync, and start
    // inDeck. The crossfader RAMP itself is driven by the editor timer. Returns
    // the plan (invalid if decks aren't ready).
    stemdeck::TransitionPlan beginAutomix (int outDeck, int inDeck);
    // The two crossfader endpoints (0=left/A .. 1=right/B) for a begun automix,
    // so the UI can ramp between them. from = out-deck side, to = in-deck side.
    static float automixCrossfaderFrom() noexcept { return 0.0f; }
    static float automixCrossfaderTo()   noexcept { return 1.0f; }

    // Hot cues — 8 per deck, seconds-domain; -1 = unset
    static constexpr int numHotCues = 8;
    void   setHotCue (int deckIndex, int cueIndex, double seconds);
    void   clearHotCue (int deckIndex, int cueIndex);
    double getHotCue (int deckIndex, int cueIndex) const;   // -1 = unset
    void   jumpToHotCue (int deckIndex, int cueIndex);

    // Loop
    void setDeckLoopSeconds (int deckIndex, double inSeconds, double outSeconds);
    void setDeckLoopActive (int deckIndex, bool active);
    void clearDeckLoop (int deckIndex);
    bool isDeckLoopActive (int deckIndex) const;
    double getDeckLoopInSeconds (int deckIndex) const;
    double getDeckLoopOutSeconds (int deckIndex) const;

    // Tap tempo
    void tapTempo();

    bool beginMasterRecording (const juce::File& destinationFile);
    juce::File stopMasterRecording();
    bool isMasterRecording() const noexcept { return masterRecorder.isRecording(); }
    juce::File getMasterRecordingTarget() const { return masterRecorder.getTargetFile(); }
    juce::File getMicRecordingTarget() const { return micRecorder.getTargetFile(); }
    juce::File getLastPerformanceEventLogFile() const { return lastPerformanceEventLogFile; }
    juce::File getLastArrangementFile() const { return lastArrangementFile; }
    juce::File getLastDeckRecordingFile (int deckIndex) const;
    int64_t getRecordedSamples() const noexcept { return recordingSampleCounter.load (std::memory_order_relaxed); }
    double getRecordingSampleRate() const noexcept { return currentSampleRate; }

    juce::String getDeckPath (int deckIndex) const;
    juce::String getDeckStemPath (int deckIndex, int stemLaneIndex) const;
    double getDeckPlayheadSeconds (int deckIndex) const;
    double getDeckTotalSeconds (int deckIndex) const;
    juce::StringArray getStemLaneLabels() const;
    juce::StringArray getKeyTargetChoices() const;
    juce::StringArray getTempoInterpretationChoices() const;
    juce::StringArray getSyncModeChoices() const;
    TrackAnalysis getDeckAnalysis (int deckIndex) const;
    TrackAnalysis getDeckLiveAnalysis (int deckIndex) const;
    TrackAnalysis getDeckActiveAnalysis (int deckIndex) const;

    // The DeckPlayer living inside channel `deckIndex` (TrackDeck channels only).
    // Returns a null fallback for an out-of-range index or a non-TrackDeck channel,
    // so existing deck-indexed call sites stay safe during the channel migration.
    DeckPlayer&       deckAt (int deckIndex) noexcept;
    const DeckPlayer& deckAt (int deckIndex) const noexcept;

    // ── Channel list (message thread) ────────────────────────────────────────
    // Add/remove is zero-dropout: a new immutable snapshot is published atomically
    // and a retired channel is reclaimed only after the audio thread has moved on
    // (see GraphSnapshot.h + the StemDeckChannelDropout gate).
    int  getNumChannels() const noexcept { return (int) channels.size(); }
    static constexpr int getMaxChannels() noexcept { return kMaxChannels; }
    int  addChannel (ChannelType type = ChannelType::trackDeck);   // -> new index, or -1 if full
    bool removeChannel (int index);
    // ── Live MIDI capture (edit what you played, after you played it) ────────
    // Runs alongside master recording; notes are timestamped in samples and
    // converted to beats on read, so a later tempo change re-renders correctly.
    std::vector<MidiCapture::CapturedNote> getCapturedMidiNotes (double bpm) const;
    int  getCapturedMidiEventCount() const noexcept { return midiCapture.getNumEvents(); }
    bool didMidiCaptureOverflow() const noexcept    { return midiCapture.overflowed(); }

    juce::String getChannelName (int index) const;
    ChannelType  getChannelType (int index) const;
    // The channel's body, for type-specific UI (Input gain/monitor, instrument
    // activity). Message thread only. Returns nullptr for an invalid index.
    ChannelBody* getChannelBody (int index) const;
    // Mute / solo live on the Channel frame, so they apply to every channel type and
    // solo can be arbitrated mix-wide (non-soloed channels drop out of the master).
    void toggleChannelMute (int index);
    void toggleChannelSolo (int index);

    // ── A1 phase 3: event dispatch ───────────────────────────────────────────
    // The single mutator for converted controls. applyEvent() is used by BOTH the
    // live path and replay, which is what makes a replay reproduce a performance.
    stemdeck::events::Dispatcher& dispatcher() noexcept { return *eventDispatcher; }
    void applyEvent (const stemdeck::events::Event& e);
    // The raw mutation behind ParamId::cfSide. Called only by applyEvent (live and
    // replay) and by state load; never directly by a control.
    void applyDeckCfSide (int deckIndex, int side);
    ChannelBody* bodyAt (int index) noexcept
    {
        return juce::isPositiveAndBelow (index, (int) channels.size()) && channels[(size_t) index]
             ? channels[(size_t) index]->getBody() : nullptr;
    }

    // Dispatch helpers for controls with no APVTS parameter behind them, so the
    // editor emits an event instead of poking a body setter directly.
    void setInputGainDispatched      (int channelIndex, float linear);
    void setInputMonitorDispatched   (int channelIndex, bool on);
    void setInputSourceDispatched    (int channelIndex, int deviceChannel);
    void setInstGainDispatched       (int channelIndex, float linear);
    void setInstMidiChannelDispatched(int channelIndex, int midiChannel);
    void setInsertBypassDispatched   (int chainIndex, int insertPos, bool bypassed);

    // ── APVTS -> event bridge (A1 phase 3) ───────────────────────────────────
    // Most controls are APVTS-attached: the UI moves a slider and the attachment
    // writes the parameter. Converting those one call site at a time would mean
    // unpicking every attachment. Instead the processor listens, and turns a
    // parameter change into an event — one bridge converts them all.
    //
    // applyEvent writes the parameter back with `applyingEvent` set, so the
    // resulting notification is not re-dispatched into an infinite loop.
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    struct ParamMapping
    {
        bool valid = false;
        stemdeck::events::ParamId id {};
        stemdeck::events::Scope   scope;
    };
    static ParamMapping mapApvtsId (const juce::String& apvtsId);
    static juce::String apvtsIdFor (const stemdeck::events::Event& e);

    std::atomic<bool> applyingEvent { false };
    bool isChannelMuted (int index) const;
    bool isChannelSoloed (int index) const;
    int          getNumTrackDecks() const;   // leading TrackDeck channels (the deck row)
    // Loaded audio for a deck (for the arrangement view's waveforms). May be null.
    std::shared_ptr<const DeckPlayer::LoadedAudio> getDeckLoadedAudio (int deckIndex) const
    { return juce::isPositiveAndBelow (deckIndex, numDecks) ? deckAt (deckIndex).getLoadedAudio() : nullptr; }
    // Per-stem loaded audio (offline separation), for the two-zone waveform's stem
    // columns. Null when that lane has no separated file. lane = drums/bass/music/vocals.
    std::shared_ptr<const DeckPlayer::LoadedAudio> getDeckStemLoadedAudio (int deckIndex, int lane) const
    { return juce::isPositiveAndBelow (deckIndex, numDecks) ? deckAt (deckIndex).getStemAudio (lane) : nullptr; }
    void refreshLiveKeyAnalysis();
    juce::String getDeckKeyInfo (int deckIndex) const;
    juce::String getDeckTimePitchStatus (int deckIndex) const;
    juce::String getDeckBeatSyncStatus (int deckIndex) const;

    // Measured beat-phase alignment for the deck's sync ring (message thread).
    //  offset: signed error to the nearest grid beat, normalised so +/-1.0 == +/-half beat.
    //  locked: true when a grid-locking mode holds phase; free-drift reports false.
    //  active: sync is engaged AND the host transport is running.
    struct DeckSyncPhase { float offset = 0.0f; bool locked = false; bool active = false; };
    DeckSyncPhase getDeckSyncPhase (int deckIndex) const;
    juce::String getWarpEngineStatus() const;
    juce::StringArray getSupportedPluginHostFormats() const;
    juce::String getPluginHostStatus() const;
    juce::String getStemProviderStatus() const;
    bool isNeuralStemAvailable() const;   // neural model present → AI split uses the net, not DSP
    juce::String getDeckStemProviderStatus (int deckIndex) const;
    // 0..1 separation progress for the deck's AI-split ring. Returns 1.0 when no job
    // is active (idle/complete); a fraction (ready/total core stems, clamped so it
    // reads as in-progress) while a split is queued or running.
    float getDeckStemSeparationProgress (int deckIndex) const;
    juce::Array<juce::PluginDescription> getKnownHostPlugins() const;
    juce::File getPluginHostCacheFile() const;
    bool loadPluginHostCache();
    bool savePluginHostCache() const;
    HostGraph::ScanResult scanPluginHostDefaultLocations (int maxPluginsPerFormat = 32, bool recursive = true);
    // Sattari-only scan — the safe path (third-party plugins crash the host mid-scan).
    HostGraph::ScanResult scanSattariPlugins() { return hostGraph.scanSattariOnly(); }
    juce::StringArray getPluginHostChainNames() const;
    void loadHostPluginToChainAsync (int chainIndex, int pluginIndex, HostGraph::LoadCallback callback);
    void clearHostPluginChain (int chainIndex);
    // FX-rack introspection for the Sattari FX panel (chainIndex maps to HostGraph::Chain).
    int getChainPluginCount (int chainIndex) const { return hostGraph.getChainPluginCount ((HostGraph::Chain) chainIndex); }
    juce::String getChainPluginName (int chainIndex, int i) const { return hostGraph.getChainPluginName ((HostGraph::Chain) chainIndex, i); }
    juce::AudioProcessor* getChainPluginProcessor (int chainIndex, int i) const { return hostGraph.getChainPluginProcessor ((HostGraph::Chain) chainIndex, i); }
    void removeChainPlugin (int chainIndex, int i)
    {
        hostGraph.removeChainPlugin ((HostGraph::Chain) chainIndex, i);
        refreshLatencyCompensation();   // chain latency changed
    }
    void moveChainPlugin (int chainIndex, int from, int to)
    {
        hostGraph.moveChainPlugin ((HostGraph::Chain) chainIndex, from, to);
        refreshLatencyCompensation();   // order changed; PDC is unchanged in total but re-read
    }
    // Bypass keeps the node in the graph, so chain latency does NOT change — a bypassed
    // plugin still reports its delay. Deliberately no refreshLatencyCompensation() here.
    void setChainPluginBypassed (int chainIndex, int i, bool b)
    { hostGraph.setChainPluginBypassed ((HostGraph::Chain) chainIndex, i, b); }
    bool isChainPluginBypassed (int chainIndex, int i) const
    { return hostGraph.isChainPluginBypassed ((HostGraph::Chain) chainIndex, i); }
    juce::String getPadPath (int padIndex) const;
    juce::StringArray getSupportedAudioExtensions() const;
    TransportSync::Snapshot getTransportSnapshot() const { return sync.getSnapshot(); }

    // Master output peak (post-limiter) for the UI meters; ch 0=L, 1=R.
    float getMasterPeak (int ch) const noexcept { return masterPeak[(size_t) juce::jlimit (0, 1, ch)].load (std::memory_order_relaxed); }
    // Per-deck output peak (pre-crossfader) for the console channel meters.
    float getDeckPeak (int deckIndex, int ch) const noexcept
    { return deckPeak[(size_t) juce::jlimit (0, numDecks - 1, deckIndex)][(size_t) juce::jlimit (0, 1, ch)].load (std::memory_order_relaxed); }
    // Beat-grid positions (source seconds) for drawing the grid on a deck waveform.
    std::vector<double> getDeckBeatGridSeconds (int deckIndex) const;

    // Smart mastering readouts for the MASTER OUTPUT panel.
    float getSmartMasterLufs() const noexcept { return smartMaster.getShortTermLufs(); }
    float getSmartMasterGr() const noexcept   { return smartMaster.getGainReductionDb(); }

    // Live BS.1770 loudness (worker thread) + safety-limiter gain reduction, for the
    // MASTER OUT panel. Integrated accumulates over the session; reset restarts it.
    double getLiveIntegratedLufs() const noexcept { return liveLoudness.getIntegratedLufs(); }
    double getLiveMomentaryLufs()  const noexcept { return liveLoudness.getMomentaryLufs(); }
    double getLivePeakDb()         const noexcept { return liveLoudness.getPeakDb(); }
    void   resetLiveLoudness() noexcept           { liveLoudness.resetIntegration(); }
    float  getLimiterGrDb() const noexcept        { return limiterGrDb.load (std::memory_order_relaxed); }

    // True when no offline stems are loaded and real-time separation is active.
    bool isDeckRtssActive (int deckIndex) const;
    // True when neural (demucs-cached) stem files are loaded on this deck.
    bool deckHasNeuralStems (int deckIndex) const;
    void setDeckRtssSensitivity (int deckIndex, float sensitivity);   // 0..1

    // Hardware MIDI controller mapping (learn + apply).
    MidiMappingManager& getMidiMapping() noexcept { return midiMapping; }

    // Tracktion Engine access — message-thread safe after construction.
    // Returns nullptr only if TE failed to initialise (should never happen).
    // False when another StemDeck instance in this process owns the single Tracktion
    // engine. The arrangement transport is unavailable; everything else works.
    bool hasArrangementEngine() const noexcept { return teWrapper != nullptr; }
    bool arrangementEngineTakenByOtherInstance() const noexcept { return teEngineUnavailable; }

    tracktion::Edit*      getTeEdit()      noexcept { return teWrapper ? teWrapper->edit.get()      : nullptr; }
    tracktion::Engine*    getTeEngine()    noexcept { return teWrapper ? &teWrapper->engine         : nullptr; }
    tracktion::TransportControl* getTeTransport() noexcept
    {
        return teWrapper && teWrapper->edit ? &teWrapper->edit->getTransport() : nullptr;
    }

    // Arrangement playback — plays the edited multi-clip arrangement back through
    // the master out via a sample-accurate, RT-safe scheduler.
    struct ArrangementClipSpec
    {
        juce::File file;
        double timelineStartSeconds = 0.0;
        double sourceStartSeconds   = 0.0;
        double lengthSeconds        = 0.0;
        float  gain = 1.0f;
        double fadeInSeconds  = 0.0;
        double fadeOutSeconds = 0.0;
        int    lane = 0;   // which TE audio track (0..numDecks-1)
    };
    void buildAndStartArrangement (std::vector<ArrangementClipSpec> clips, double minLengthSeconds = 0.0, double startSeconds = 0.0);
    void stopArrangement();
    void setMidiNotes (const std::vector<MidiClipPlayer::SourceNote>& notes, double bpm);
    // Real-time per-lane volume for TE arrangement tracks (0..numDecks-1).
    void setTeArrangementLaneGain (int lane, float gainLinear);
    void setMidiLaneGain (int lane, float gainLinear) noexcept { midiClipPlayer.setLaneGain (lane, gainLinear); }
    void setMetronomeEnabled (bool e) noexcept { metronome.setEnabled (e); }
    bool isMetronomeEnabled() const noexcept   { return metronome.isEnabled(); }
    void setMetronomeBpm (double b) noexcept    { metronome.setBpm (b); }
    bool   isArrangementPlaying()         const noexcept { return arrangementPlayer.isPlaying(); }
    double getArrangementPositionSeconds() const noexcept { return arrangementPlayer.getPositionSeconds(); }
    double getArrangementLengthSeconds()   const noexcept { return arrangementPlayer.getLengthSeconds(); }
    void   setArrangementPositionSeconds (double s);
    void   setArrangementLoop (double startSec, double endSec, bool enabled);

    // Intro/Outro DJ markers — stored as APVTS state properties (like hot cues)
    void   setIntroEnd     (int deckIndex, double seconds);
    double getIntroEnd     (int deckIndex) const;
    void   clearIntroEnd   (int deckIndex);
    void   setOutroStart   (int deckIndex, double seconds);
    double getOutroStart   (int deckIndex) const;
    void   clearOutroStart (int deckIndex);

    // Talkover ducking: auto-attenuates deck output when mic exceeds threshold
    void setTalkoverEnabled (bool on) noexcept { talkoverEnabled.store (on, std::memory_order_relaxed); }
    bool isTalkoverEnabled() const noexcept    { return talkoverEnabled.load (std::memory_order_relaxed); }
    void setTalkoverThresholdDb (float db) noexcept { talkoverThresholdDb.store (juce::jlimit (-80.0f, 0.0f, db), std::memory_order_relaxed); }
    void setTalkoverDuckDb (float db) noexcept      { talkoverDuckDb.store (juce::jlimit (-60.0f, 0.0f, db), std::memory_order_relaxed); }

    // Crossfader side assignment per deck: 0=A (left), 1=Center, 2=B (right)
    void setDeckCfSide (int deckIndex, int side);
    int  getDeckCfSide (int deckIndex) const;   // RT-safe: reads an atomic, not the tree
    void refreshCfSideCache();

    // Deck slip mode + loop roll — thin pass-throughs to DeckPlayer
    void setDeckSlipEnabled (int deckIndex, bool on) noexcept;
    bool isDeckSlipEnabled  (int deckIndex) const noexcept;
    void beginDeckLoopRoll  (int deckIndex, double lengthSeconds) noexcept;
    void endDeckLoopRoll    (int deckIndex) noexcept;

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
    // runNeuralInline=false returns as soon as the fast DSP analysis is done, so the
    // deck is playable immediately (§A3); pair it with refineAnalysisAsync().
    // Decode with no analysis — the only work that must precede audible playback.
    std::shared_ptr<DeckPlayer::LoadedAudio> decodeAudioFile (const juce::File& file);
    // Fill a standalone analysis (cache lookup, DSP passes, optional neural key).
    void analyseInto (TrackAnalysis& out, const juce::AudioBuffer<float>& audio,
                      double sampleRate, const juce::File& file, bool runNeuralInline);
    std::shared_ptr<const DeckPlayer::LoadedAudio> loadAudioFile (const juce::File& file,
                                                                  bool runNeuralInline = true);
    // Run the neural beat/key models off-thread for an already-playing deck and
    // hot-swap the refined grid/key in. No audio interruption: the refined analysis
    // is published through an atomic shared_ptr the audio thread reads per block.
    void refineAnalysisAsync (int deckIndex, const juce::File& file);
    void restoreFileAssignmentsFromState();
    void setStatePathProperty (const juce::Identifier& property, const juce::String& path);
    void applyPremiumStemMix (int deckIndex);
    // StringRef (not String) so literal-id reads on the audio thread don't heap-
    // allocate a juce::String on every call.
    float getFloatParam (juce::StringRef id) const;
    bool getBoolParam (juce::StringRef id) const;
    TrackAnalysis getDeckLiveAnalysisFromAtomics (int deckIndex) const;
    void applyLimiter (juce::AudioBuffer<float>& buffer, float ceilingDb);
    void initialisePerformanceCaptureState();
    void updatePerformanceCaptureState (int64_t blockStartSample, const TransportSync::Snapshot& snapshot);
    void logPerformanceEvent (int64_t sample, const juce::String& type, const juce::String& deck = {},
                              const juce::String& parameter = {}, const juce::String& value = {},
                              const juce::String& path = {}, const juce::String& details = {});

    static juce::String hotCueProperty (int deckIndex, int cueIndex);
    static void migrateStemGainsDbToPercent (juce::ValueTree& tree);   // v1 dB -> v2 percent

    // Per-deck built-in FX (reverb + echo) now lives in DeckFX.h so a Channel /
    // TrackDeckBody can own one without depending on the whole processor.

    // MIDI notes staged by setMidiNotes() and consumed inside buildAndStartArrangement()
    // to populate the TE MIDI+4OSC synth tracks.  Message-thread only.
    std::vector<MidiClipPlayer::SourceNote> pendingMidiNotes;
    double pendingMidiBpm = 120.0;
    std::atomic<bool> usingTeMidi { false };   // processBlock: skip MidiClipPlayer when true
    std::atomic<bool> teStopRequested { false }; // audio→message: defer TE transport stop (RT-safe)

public:
    // Message thread: services deferred audio-thread requests (e.g. stopping the TE
    // transport when the arrangement auto-stops). Called from the editor timer.
    void serviceDeferredActions();
private:

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioFormatManager formatManager;
    juce::ThreadPool loadPool { 4 };

    // Async load/stem completion callbacks capture `this` and fire via callAsync on the
    // message thread. A job can finish and post its closure just as the processor is torn
    // down; this flag (kept alive by each closure's shared_ptr copy) is cleared FIRST in
    // the destructor so a late closure skips instead of touching freed state.
    std::shared_ptr<std::atomic<bool>> aliveFlag { std::make_shared<std::atomic<bool>> (true) };

    ArrangementPlayer arrangementPlayer;
    MidiClipPlayer    midiClipPlayer;
    Metronome         metronome;
    juce::int64       lastArrPos = 0;     // for loop-wrap detection (all-notes-off)
    bool              arrWasPlaying = false;
    // Per-deck scratch buffers for RTSS output (allocated in prepareToPlay)

    std::vector<juce::int64> tapTimes;   // millisecond timestamps of recent taps
    MidiMappingManager midiMapping;
    void buildMidiTargets();

    // ── Channels (channel architecture) ──────────────────────────────────────
    // The per-deck machinery now lives inside Channel/TrackDeckBody. `channels` is
    // the message-thread authoritative list; `channelGraph` publishes an immutable
    // snapshot the audio thread iterates lock-free (zero-dropout add/remove).
    // Bootstrapped with numDecks TrackDeck channels so existing deck-indexed code
    // keeps working while add/remove is layered on.
    std::vector<std::shared_ptr<Channel>> channels;
    ChannelGraph channelGraph;
    DeckPlayer   nullDeck;   // out-of-range fallback for deckAt()
    juce::MidiBuffer* currentBlockMidi = nullptr;   // audio thread only: this block's MIDI, for channel FX chains
    std::atomic<bool> hasInputChannels { false };   // any Input channel present → capture device input
    MidiCapture       midiCapture;                  // live MIDI recorded during a take

    // Republish `channels` as a new immutable snapshot (message thread only).
    void publishChannels();
    // Build a fully-wired channel (body + frame hook) bound to parameter slot `index`.
    std::shared_ptr<Channel> makeChannel (int index, ChannelType type);
    // Rebuild the channel list from the persisted <CHANNELS> state child.
    void restoreChannelsFromState();
    // Recompute plugin-delay compensation across channels and report total latency to
    // the host. Call whenever a channel's FX chain or the channel list changes.
    void refreshLatencyCompensation();

    PadBank padBank;
    MasterRecorder masterRecorder;
    MasterRecorder micRecorder;
    std::array<MasterRecorder, kMaxChannels> deckRecorders;
    PerformanceRecorder performanceRecorder;
    TransportSync sync;
    WarpEngine warpEngine;
    HostGraph hostGraph;
    MasterFx masterFx;
    SmartMaster smartMaster;
    stemdeck::LiveLoudnessMeter liveLoudness;
    std::atomic<float> limiterGrDb { 0.0f };   // safety-limiter peak GR (dB), UI-read
    // Per-stem FX (RTSS path): [deck][stemIndex 0..3 = drums/bass/music/vocals].
    // Post-limiter master peaks for UI metering (decayed on the audio thread).
    std::array<std::atomic<float>, 2> masterPeak { 0.0f, 0.0f };
    // Per-deck peaks (pre-crossfader) for the console channel meters.
    std::array<std::array<std::atomic<float>, 2>, kMaxChannels> deckPeak {};
    StemProvider stemProvider;

    double currentSampleRate = 44100.0;
    float limiterEnvelope = 1.0f;

    // Talkover ducking state (audio-thread + message-thread atomics)
    std::atomic<bool>  talkoverEnabled      { false };
    std::atomic<float> talkoverThresholdDb  { -40.0f };
    std::atomic<float> talkoverDuckDb       { -12.0f };
    float talkoverEnvelope = 0.0f;   // audio-thread: smoothed mic level [0,1]
    float talkoverDuckGain = 1.0f;   // audio-thread: current duck gain [0,1]
    std::array<std::atomic<int>, kMaxChannels> liveKeyPitchClass;
    std::array<std::atomic<int>, kMaxChannels> liveKeyIsMinor;
    std::array<std::atomic<float>, kMaxChannels> liveKeyConfidence;
    std::array<int64_t, kMaxChannels> lastLiveKeyAnalysisMs {};
    // Guards against piling up live-key analysis jobs; the heavy FFT runs on loadPool
    // (drained in the dtor) instead of the UI timer, so it can't stall the UI.
    std::array<std::atomic<bool>, kMaxChannels> liveKeyAnalysisInFlight {};
    std::atomic<int64_t> recordingSampleCounter { 0 };
    int64_t transportSampleCounter = 0;
    juce::File lastPerformanceEventLogFile;
    juce::File lastArrangementFile;
    std::array<juce::File, kMaxChannels> lastDeckRecordingTargets;
    // Pre-allocated audio-thread scratch (sized in prepareToPlay) — avoids per-block
    // heap allocation in the mic-capture and Tracktion-mix paths.
    juce::AudioBuffer<float> micScratch;
    juce::AudioBuffer<float> teScratch;
    juce::MidiBuffer teMidiScratch;

    // Per-deck beat-grid cache. makeGrid copies two vectors + sorts, so rebuild it
    // only when its inputs change (audio reload or a grid-param edit) rather than
    // every audio block. Audio-thread only.
    struct GridCacheKey
    {
        const void* analysisId = nullptr;
        float bpm = -1.0f, beatOffset = -1.0f, downbeat = -1.0f;
        int   interp = -1, syncMode = -1;
        bool operator!= (const GridCacheKey& o) const noexcept
        {
            return analysisId != o.analysisId || bpm != o.bpm || beatOffset != o.beatOffset
                || downbeat != o.downbeat || interp != o.interp || syncMode != o.syncMode;
        }
    };

    // Pre-concatenated APVTS parameter ids per deck. "deck + \"...\"" heap-allocates
    // a juce::String; caching keeps processBlock's per-deck reads allocation-free.
    struct DeckParamIds
    {
        juce::String bpm, beatOffset, downbeat, tempoInterp, syncMode, keyTarget, pitch,
                     play, liveKey, keyLock, volume, fader, low, mid, high, filter, revMix, echoMix;
        juce::String displayName;   // "Deck A" — cached constant, so the audio-thread
                                    // capture path doesn't rebuild it every block.
        std::array<juce::String, DeckPlayer::numStemLanes> stemGain, stemPitch, stemFilt, stemSend;
    };
    // Trained neural key model (optional). Applied on top of TrackAnalyzer's
    // DSP key when the model is installed; inert otherwise.
    NeuralKeyEngine neuralKey;
    // Trained neural beat/downbeat tracker (optional). Overrides the DSP
    // beat grid when the model is installed; inert otherwise.
    NeuralBeatEngine neuralBeat;
    // Persistent analysis cache — a re-load skips DSP + neural analysis.
    AnalysisCache analysisCache;

    std::array<DeckParamIds, kMaxChannels> deckIds;
    void cacheDeckParamIds();

    // Pad gains were read as getFloatParam("pad" + String(i+1) + "Gain") inside
    // processBlock — that heap-allocates juce::String temporaries on the audio
    // thread every block. Cache the atomic pointers instead: no allocation AND
    // no per-block APVTS string lookup.
    std::array<std::atomic<float>*, PadBank::numPads> padGainParams {};

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

    std::array<DeckPerformanceState, kMaxChannels> lastDeckPerformanceState;
    bool lastMicMonitor = false;
    bool lastHostSync = true;
    bool lastBeatSync = false;
    float lastCrossfader = 0.5f;
    float lastMasterBpm = 120.0f;
    float lastMicGain = -12.0f;

    // ── Tracktion Engine wrapper ──────────────────────────────────────────────
    struct PluginEngineBehaviour : public tracktion::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override { return false; }
        bool addSystemAudioIODeviceTypes() override { return false; }
    };

    struct TeWrapper
    {
        TeWrapper()
            : audioInterface (engine.getDeviceManager().getHostedAudioDeviceInterface())
        {
            jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
            audioInterface.initialise ({});
            edit->getTransport().ensureContextAllocated (true);
        }

        tracktion::Engine engine { JucePlugin_Name, nullptr, std::make_unique<PluginEngineBehaviour>() };
        std::unique_ptr<tracktion::Edit> edit { tracktion::Edit::createSingleTrackEdit (engine) };
        tracktion::HostedAudioDeviceInterface& audioInterface;
        tracktion::ExternalPlayheadSynchroniser playheadSync { *edit };
    };

    // Audio-thread mirror of the persisted deck<N>CfSide properties.
    std::array<std::atomic<int>, numDecks> cfSideCache {};
    std::unique_ptr<stemdeck::events::Dispatcher> eventDispatcher;

    std::unique_ptr<TeWrapper> teWrapper;
    // Tracktion Engine is one-per-process. This instance owns it, or it went without.
    bool ownsTeEngine = false;
    bool teEngineUnavailable = false;

    static bool claimTeEngine() noexcept;
    static void releaseTeEngine() noexcept;

    void ensureTeWrapperOnMessageThread();
    void prepareTeWrapper();
    int  preparedBlockSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPluginProcessor)
};
