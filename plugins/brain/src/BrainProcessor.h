#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariSongKeyEstimator.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariHpcpChroma.h>
#include <sattari_audio_core/SattariKeyDetector.h>
#include <sattari_audio_core/SattariRealtime.h>
#include <array>
#include <atomic>
#include <thread>

// =============================================================================
//  Sattari Compass — the session conductor (detect + edit + publish FULL state)
// -----------------------------------------------------------------------------
//  Sattari Key is the focused detector and lower-priority fallback publisher.
//  Compass is the conductor: it detects key/mode/confidence, major/minor chord
//  evidence, reference tuning, energy and musical onsets; reads host BPM + time
//  signature; and lets the user EDIT or override the rest of the session context
//  — chord, time signature, swing, section, energy, maqam and reference — then
//  broadcasts one authoritative SessionState (schema v2) to the
//  cross-plugin bus that every follower plugin reads. Audio passes through
//  bit-exact; Compass never touches the signal, it only listens and publishes.
//
//  Compatibility: the target, product code, state tree, `brain.*` parameter ids
//  and cross-plugin publisher id remain unchanged. Existing sessions therefore
//  reopen exactly as before while hosts display the clearer Compass identity.
//
//  RT-safety: processBlock only feeds a lock-free ring + stores atomics (no
//  alloc, no locks after prepare). All FFT / detection / file publishing happens
//  on a low-priority worker thread on a ~700 ms cadence.
// =============================================================================
class SattariBrainAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariBrainAudioProcessor();
    ~SattariBrainAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Compass"; }
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

    // --- Param choice vocabularies (shared by processor + editor) -------------
    static juce::StringArray rootChoices();       // C..B
    static juce::StringArray scaleChoices();       // Major / Minor
    static juce::StringArray chordRootChoices();   // None, C..B
    static juce::StringArray chordQualityChoices();// none,maj,min,dom7,min7,maj7,dim,aug,sus
    static juce::StringArray timeSigDenChoices();  // 1,2,4,8,16
    static juce::StringArray sectionChoices();     // None,Intro,Verse,Prechorus,Chorus,Bridge,Outro
    static int timeSigDenValue (int choiceIndex) noexcept; // choice index -> real denominator

    // Run one detection pass over the current ring (FFT -> chroma -> key +
    // reference-tuning estimate). Called by the worker; public for the offline
    // contract if it wants to force a scan.
    void analyseNow();
    void requestRescan() noexcept { rescan.store (true); }

    // Live readout for the editor.
    struct Detected
    {
        bool   valid = false;
        int    root = 0;
        bool   minor = false;
        double confidence = 0.0;
        bool   chordValid = false;
        int    chordRoot = -1;
        int    chordQuality = 0;
        double chordConfidence = 0.0;
        float  energy = 0.0f;
        juce::int64 transientId = 0;
        bool   publishing = false;
        double detuneCents = 0.0;
        double bpm = 0.0;
        int    tsNum = 0;   // host time signature (0 = host did not report)
        int    tsDen = 0;
    };
    Detected detected() const noexcept;

    // Last completed bus attempt. Gated/off publication does not manufacture
    // an attempt; the contract also reports the raw gate values on timeout.
    sattari::xplugin::SessionPublishResult lastPublicationResult() const noexcept
    { return publicationResult.load (std::memory_order_relaxed); }
    unsigned int publicationAttemptCount() const noexcept
    { return publicationAttempts.load (std::memory_order_relaxed); }

    sattari::midi::SattariMidiLearn midiLearn { apvts };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    void  publishCurrent();
    void  refreshDetectedFrame() noexcept;

    juce::AudioProcessorValueTreeState apvts;

    double currentSampleRate = 44100.0;
    static constexpr int fftOrder = 12;            // 4096-point
    static constexpr int fftSize  = 1 << fftOrder;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::array<std::atomic<float>, fftSize> ring;
    std::array<float, fftSize * 2> scratch {};
    std::array<float, fftSize> hann {};
    std::atomic<int> writePos { 0 };
    sattari::dsp::HpcpChromagram hpcp;             // worker-thread-only
    double runningDetune = 0.0;                    // worker-thread-only (cents)

    //  Whole-session key. Measured on 1337 labelled tracks the SongKeyEstimator
    //  reaches 57.6% exact / 65.12 MIREX against the Aarden/HPCP path's
    //  39.2% / 46.69, so the conductor no longer publishes the weakest key in its
    //  own product family. The HPCP path above is retained: it still produces the
    //  detune estimate and the triad evidence, and it is the fallback until the
    //  estimator has heard enough audio to have an opinion.
    //
    //  The estimator wants CONTINUOUS audio, and `ring` above is one 4096-sample
    //  FFT frame (~85 ms) while the worker wakes on a ~700 ms cadence — it would
    //  see roughly one eighth of the session. So it gets its own feed: processBlock
    //  writes every mono sample, the worker drains whatever has arrived. Single
    //  producer, single consumer, no locks and no allocation on the audio thread.
    sattari::dsp::SongKeyEstimator songKey;        // worker-thread-only
    std::vector<std::atomic<float>> keyFeed;       // sized in prepareToPlay
    std::atomic<std::int64_t> keyFeedWrite { 0 };  // audio thread writes
    std::int64_t keyFeedRead = 0;                  // worker thread reads
    std::vector<float> keyDrain;                   // worker scratch
    std::atomic<std::int64_t> keyFeedDropped { 0 };// audio outran the worker
    std::atomic<int> songKeyFrames { 0 };          // frames the estimator has analysed

    // Detection readout (audio/worker -> UI), all lock-free.
    std::atomic<sattari::xplugin::SessionPublishResult> publicationResult {
        sattari::xplugin::SessionPublishResult::notAttempted };
    std::atomic<unsigned int> publicationAttempts { 0 };
    std::atomic<int>    detRoot { 0 };
    std::atomic<bool>   detMinor { false }, detValid { false }, rescan { false }, publishingNow { false };
    std::atomic<double> detConf { 0.0 };
    std::atomic<double> detDetuneCents { 0.0 };    // tuning-reference deviation vs A440
    std::atomic<int>    detChordRoot { -1 }, detChordQuality { 0 };
    std::atomic<bool>   detChordValid { false };
    std::atomic<double> detChordConfidence { 0.0 };
    std::atomic<float>  detEnergy { 0.0f };
    std::atomic<juce::int64> transientId { 0 };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<int>    hostTsNum { 0 }, hostTsDen { 0 };
    sattari::rt::AtomicSnapshot<Detected> detectedFrame;

    // Audio-thread-only onset state. The counter is published atomically; the
    // envelope and refractory timer never cross threads.
    float onsetEnvelope = 0.0f;
    int onsetRefractorySamples = 0;

    // Confidence-gated + hysteresis publish state (worker thread only) — the same
    // logic Sattari Key uses so the detected key never flickers the whole suite.
    int  pubRoot = -1, pubScale = 0;
    int  candRoot = -1, candScale = 0, candCount = 0;
    static constexpr double kMinPublishConfidence = 0.02;
    static constexpr int    kStableAnalyses = 2;

    std::thread worker;
    std::atomic<bool> workerRunning { false };
    void stopWorker();
    void workerLoop();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariBrainAudioProcessor)
};
