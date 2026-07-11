#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariChromagram.h>
#include <sattari_audio_core/SattariKeyDetector.h>
#include <array>
#include <atomic>
#include <thread>

// =============================================================================
//  Sattari Key — master-track key scanner + suite key publisher
// -----------------------------------------------------------------------------
//  Drop it on the master bus: it scans the full mix (its MAIN input — no
//  sidechain routing), builds a rolling chromagram, runs the shared 24-key
//  Krumhansl detector, and publishes ONE authoritative key to the cross-plugin
//  session bus. Every other Sattari plugin in "Follow Song Key" mode then locks
//  to it — "scan the master, get the key, apply to the whole project". Audio
//  passes through untouched.
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
    void requestRescan() noexcept { rescan.store (true); }

    // Live readout for the editor.
    struct Detected { bool valid=false; int root=0; bool minor=false; double confidence=0.0; bool publishing=false; double detuneCents=0.0; };
    Detected detected() const noexcept;

    sattari::midi::SattariMidiLearn midiLearn { apvts };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    void  publishCurrent();

    juce::AudioProcessorValueTreeState apvts;

    double currentSampleRate = 44100.0;
    static constexpr int fftOrder = 12;            // 4096-point
    static constexpr int fftSize  = 1 << fftOrder;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::array<std::atomic<float>, fftSize> ring;
    std::array<float, fftSize * 2> scratch {};
    std::array<float, fftSize> hann {};
    std::atomic<int> writePos { 0 };
    double running[12] = { 0 };                    // background-thread-only
    double runningDetune = 0.0;                    // background-thread-only (cents)

    std::atomic<int>   detRoot { 0 };
    std::atomic<bool>  detMinor { false }, detValid { false }, rescan { false }, publishingNow { false };
    std::atomic<double> detConf { 0.0 };
    std::atomic<double> detDetuneCents { 0.0 };    // tuning-reference deviation vs A440
    std::atomic<double> hostBpm { 0.0 };
    juce::int64 transientId = 0;

    // Confidence-gated + hysteresis publish state (worker thread only). The key we
    // actually broadcast (pub*) only changes when a new candidate is confident AND
    // stays the winner for kStableAnalyses cycles, so a single ambiguous frame can't
    // flip the whole suite's key.
    int  pubRoot = -1, pubScale = 0;
    int  candRoot = -1, candScale = 0, candCount = 0;
    static constexpr double kMinPublishConfidence = 0.02;
    static constexpr int    kStableAnalyses = 2;

    std::thread worker;
    std::atomic<bool> workerRunning { false };
    void stopWorker();
    void workerLoop();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariSongKeyAudioProcessor)
};
