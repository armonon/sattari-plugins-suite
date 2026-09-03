#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariChromagram.h>
#include <sattari_audio_core/SattariKeyDetector.h>
#include <sattari_audio_core/SattariKeyAnalysis.h>
#include <sattari_audio_core/SattariHpcpChroma.h>
#include <sattari_audio_core/SattariSongKeyEstimator.h>
#include <sattari_audio_core/SattariTempoEstimator.h>
#include <sattari_audio_core/SattariDynamicKeyTracker.h>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// =============================================================================
//  Sattari Key — master-track key scanner + suite key publisher
// -----------------------------------------------------------------------------
//  Drop it on the master bus for continuous listening, or choose/drop a complete
//  song for an asynchronous full-file scan. Both paths use the balanced 16 kHz
//  full-mix detector and can publish ONE authoritative key to the cross-plugin
//  session bus. Every other Sattari plugin in "Follow Song Key" mode then locks
//  to it. Live audio passes through untouched.
// =============================================================================
class SattariSongKeyAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariSongKeyAudioProcessor();
    ~SattariSongKeyAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Key"; }
    bool acceptsMidi() const override { return true; }
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

    static juce::StringArray rootChoices();
    static juce::StringArray scaleChoices();   // Major / Minor

    // Run one detection pass over the current ring (FFT -> chroma -> key). Called
    // by the background publisher thread and by the offline render proof.
    void analyseNow();
    void serviceLiveAnalysis();
    void requestRescan();

    // A file analysis is deliberately a command, not an automatable parameter.
    // It decodes and scans on its own worker so neither the audio callback nor the
    // message thread is blocked. Live mode keeps listening to the plugin's main
    // input, including a standalone app's selected audio input.
    enum class AnalysisSource { Live, File };
    bool analyseFileAsync (const juce::File&);
    void listenLive();
    void reanalyseCurrentSource();
    void setSessionLocked (bool);
    void setDynamicFollowEnabled (bool);
    void unlockSession();
    static bool isSupportedAudioFile (const juce::File&);

    // Live readout for the editor. The release build is model-free: the balanced
    // full-mix detector runs locally, with no hidden model or network dependency.
    struct Detected
    {
        bool valid = false;
        int root = 0;
        bool minor = false;
        double confidence = 0.0;
        bool alternativeValid = false;
        int alternativeRoot = 0;
        bool alternativeMinor = false;
        bool publishing = false;
        double detuneCents = 0.0;
        bool tempoValid = false;
        double bpm = 0.0;
        double tempoConfidence = 0.0;
        bool tempoFromHost = false;
        bool tempoOctaveAmbiguous = false;
        bool lockRequested = false;
        bool sessionLocked = false;
        int lockedRoot = 0;
        bool lockedMinor = false;
        AnalysisSource source = AnalysisSource::Live;
        bool analysing = false;
        double progress = 0.0;
        double segmentAgreement = 0.0;
        int segments = 0;
        bool possibleModulation = false;
        bool dynamicEnabled = false;
        bool dynamicActive = false;
        int dynamicRoot = 0;
        bool dynamicMinor = false;
        double dynamicConfidence = 0.0;
        bool dynamicPending = false;
        int dynamicPendingRoot = 0;
        bool dynamicPendingMinor = false;
        int dynamicPendingConfirmations = 0;
        double dynamicIntervalSeconds = 3.0;
        double dynamicSecondsUntilScan = 0.0;
        int dynamicRevision = 0;
        bool dynamicFastLane = false;
        int dynamicEvidenceWindows = 0;
        int dynamicRequiredConfirmations = 0;
        juce::String sourceName;
        juce::String error;
    };
    Detected detected() const;

    sattari::midi::SattariMidiLearn midiLearn { apvts };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    void  publishCurrent();

    struct OfflineResult
    {
        sattari::dsp::KeyAnalysisResult key;
        sattari::dsp::KeyCandidate alternative;
        sattari::dsp::TempoResult tempo;
        double detuneCents = 0.0;
        double segmentAgreement = 0.0;
        int segments = 0;
        bool possibleModulation = false;
    };
    bool analyseFile (const juce::File&, OfflineResult&, juce::String& error);
    static float onsetStrength (const float* magnitude, int numBins,
                                std::vector<float>& previous);
    void stopFileWorker();
    void clearPublishedDecision();
    void resetLiveKeyWindow();
    void resetDynamicDecision();

    juce::AudioProcessorValueTreeState apvts;

    double currentSampleRate = 44100.0;
    static constexpr int fftOrder = 12;            // 4096-point
    static constexpr int fftSize  = 1 << fftOrder;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::array<std::atomic<float>, fftSize> ring;
    std::array<float, fftSize * 2> scratch {};
    std::array<float, fftSize> hann {};
    std::array<float, fftSize> liveKeyInput {};
    std::atomic<int> writePos { 0 };
    std::atomic<juce::int64> totalSamplesWritten { 0 };
    double running[12] = { 0 };                    // background-thread-only
    double runningBass[12] = { 0 };                // low-band tonic evidence
    std::array<double, 12> liveChordRootEvidence {};
    double liveWindowRmsSum = 0.0;
    int liveWindowRmsFrames = 0;
    double liveOnsetBaseline = 0.0;
    juce::int64 lastHostBeatIndex = std::numeric_limits<juce::int64>::min();
    double runningDetune = 0.0;                    // background-thread-only (cents)
    std::vector<float> livePreviousSpectrum;       // worker-thread-only
    std::vector<float> liveOnsetEnvelope;          // worker-thread-only

    // --- Release key engines (worker-thread only) ----------------------------
    // SongKeyEstimator owns the balanced full-mix decision. The historical HPCP
    // path remains as a warm-up fallback and provides the A440 detune readout;
    // changing Sattari Key therefore does not change Compass or frozen Auto Pitch.
    sattari::dsp::HpcpChromagram hpcp;
    sattari::dsp::HpcpChromagram bassHpcp { 40.0, 260.0 };
    sattari::dsp::HpcpChromagram frameHpcp;
    sattari::dsp::KeyAnalysisEngine keyAnalysis;
    sattari::dsp::SongKeyEstimator liveSongKey;
    sattari::dsp::ChordAnalyzer chordAnalysis;
    sattari::dsp::DynamicKeyTracker dynamicTracker;

    std::atomic<int>   detRoot { 0 };
    std::atomic<bool>  detMinor { false }, detValid { false }, rescan { false }, publishingNow { false };
    std::atomic<double> detConf { 0.0 };
    std::atomic<bool>   detAlternativeValid { false };
    std::atomic<int>    detAlternativeRoot { 0 };
    std::atomic<bool>   detAlternativeMinor { false };
    std::atomic<double> detDetuneCents { 0.0 };    // tuning-reference deviation vs A440
    std::atomic<bool>   detTempoValid { false };
    std::atomic<double> detBpm { 0.0 };
    std::atomic<double> detTempoConfidence { 0.0 };
    std::atomic<bool>   detTempoOctaveAmbiguous { false };
    std::atomic<double> detSegmentAgreement { 0.0 };
    std::atomic<int>    detSegments { 0 };
    std::atomic<bool>   detPossibleModulation { false };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<double> hostPpq { -1.0 };
    std::atomic<bool> hostHasPpq { false };
    juce::int64 transientId = 0;

    std::atomic<AnalysisSource> analysisSource { AnalysisSource::Live };
    std::atomic<bool> fileAnalysing { false };
    std::atomic<double> fileProgress { 0.0 };
    std::atomic<bool> fileCancel { false };
    std::thread fileWorker;
    juce::File lastFile;
    juce::String fileSourceName;
    juce::String fileError;
    mutable juce::CriticalSection fileStateLock;
    mutable std::mutex analysisStateMutex;

    // Lock snapshot is guarded by analysisStateMutex. Detection may continue and
    // show a new candidate, but the session bus cannot change until the user
    // explicitly unlocks and locks again.
    bool lockSnapshotValid = false;
    int lockedRoot = 0;
    bool lockedMinor = false;
    double lockedReferenceHz = 440.0;
    double lockedBpm = 0.0;
    static constexpr double kMinPublishConfidence = sattari::dsp::kReleaseKeyConfidence;

    // Dynamic Follow state is worker-thread owned under analysisStateMutex.
    // The accepted key is deliberately separate from the latest detector
    // candidate: ambiguous or silent windows retain the last trusted decision.
    bool dynamicWasEnabled = false;
    bool dynamicDecisionValid = false;
    int dynamicRoot = 0;
    bool dynamicMinor = false;
    double dynamicConfidence = 0.0;
    int dynamicPendingRoot = 0;
    bool dynamicPendingMinor = false;
    int dynamicPendingConfirmations = 0;
    int dynamicRevision = 0;
    bool dynamicLastFastLane = false;
    int dynamicEvidenceWindows = 0;
    int dynamicRequiredConfirmations = 0;
    juce::int64 nextDynamicScanSample = 0;
    juce::int64 lastDynamicPublishAttemptMs = 0;
    bool liveWindowHasValidDecision = false;

    std::thread worker;
    std::atomic<bool> workerRunning { false };
    void stopWorker();
    void workerLoop();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariSongKeyAudioProcessor)
};
