#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariHpcpChroma.h>
#include <sattari_audio_core/SattariKeyDetector.h>
#include <sattari_audio_core/SattariRealtime.h>
#include <array>
#include <atomic>
#include <thread>

// =============================================================================
//  Sattari Brain — the session brain (detect + edit + publish the FULL state)
// -----------------------------------------------------------------------------
//  Sattari Key scans the master and publishes ONE key. Sattari Brain is that,
//  plus the whole SessionState: it detects key/mode/confidence + the reference
//  tuning from the spectrum (same shared chromagram + Krumhansl worker as Key),
//  reads host BPM + time signature, and lets the user EDIT the rest of the
//  session context — chord, time signature, swing, section, energy, maqam and
//  reference — then broadcasts one authoritative SessionState (schema v2) to the
//  cross-plugin bus that every follower plugin reads. Audio passes through
//  bit-exact; Brain never touches the signal, it only listens and publishes.
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

    const juce::String getName() const override { return "Sattari Brain"; }
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
        bool   publishing = false;
        double detuneCents = 0.0;
        double bpm = 0.0;
        int    tsNum = 0;   // host time signature (0 = host did not report)
        int    tsDen = 0;
    };
    Detected detected() const noexcept;

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

    // Detection readout (audio/worker -> UI), all lock-free.
    std::atomic<int>    detRoot { 0 };
    std::atomic<bool>   detMinor { false }, detValid { false }, rescan { false }, publishingNow { false };
    std::atomic<double> detConf { 0.0 };
    std::atomic<double> detDetuneCents { 0.0 };    // tuning-reference deviation vs A440
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<int>    hostTsNum { 0 }, hostTsDen { 0 };
    sattari::rt::AtomicSnapshot<Detected> detectedFrame;

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
