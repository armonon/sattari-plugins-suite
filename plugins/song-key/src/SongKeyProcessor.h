#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariChromagram.h>
#include <sattari_audio_core/SattariKeyDetector.h>
#include <sattari_audio_core/SattariHpcpChroma.h>
#include <sattari_audio_core/SattariLogSpec.h>
#include <sattari_audio_core/SattariKeyNetSpec.h>
#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

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
    // `neural` tells the UI which detector produced this key. Without it a missing
    // keynet_spec.onnx would silently drop the plugin onto the (weaker) chroma path
    // with no way for the user to tell — the difference is ~61% vs ~45% exact.
    struct Detected { bool valid=false; int root=0; bool minor=false; double confidence=0.0; bool publishing=false; double detuneCents=0.0; bool neural=false; };
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

    // --- Key detection engines (worker-thread only) --------------------------
    // Measured on 500 real GiantSteps tracks (SattariKeyDatasetEval), MIREXx100 /
    // exact-match: the plain linear accumulateChroma fold this plugin used to run on
    // scores 17.4 / 9.0% — barely above the ~4% chance rate for 24 keys, because
    // low-frequency FFT bins are too coarse to separate semitones. Two upgrades, both
    // already proven elsewhere in the suite, replace it:
    //   * HpcpChromagram  (tuning-corrected + harmonic sum)   56.6 / 45.2%
    //   * KeyNetSpec      (spectrogram CNN, needs ~30 s)      72.1 / 67.8%
    // The neural model is the primary; HPCP is the always-available fallback, so the
    // plugin is ~5x better than before even with no model installed.
    sattari::dsp::HpcpChromagram hpcp;

    // Neural key model. It was trained on 30 s excerpts, so a long rolling mono ring
    // (separate from the 4096-pt FFT ring) feeds it, and inference is throttled — a
    // TTA pass is 12 forward passes and there is no reason to run it every cycle.
    sattari::dsp::LogSpec    logSpec;
    sattari::dsp::KeyNetSpec keyNetSpec;
    bool keyNetSpecPresent = false;
    static constexpr double kSpecSeconds     = 30.0;   // model's training excerpt length
    static constexpr double kSpecIntervalSec = 5.0;    // re-run the net at most this often
    std::unique_ptr<std::atomic<float>[]> specRing;    // audio thread writes, worker reads
    int  specRingSize = 0;
    std::atomic<int>  specWritePos { 0 };
    std::atomic<long> specFilled   { 0 };              // total samples seen (for readiness)
    std::vector<float> specMono, specFeat;             // worker scratch (sized in prepare)
    // Throttle by AUDIO CONSUMED, not by tick count: analyseNow() is called by the
    // worker every ~700 ms in the plugin but back-to-back by offline harnesses, so a
    // per-call countdown would fire the net at wildly different rates. Counting
    // samples makes the cadence identical in both.
    long specLastRunSamples = -1;                      // specFilled when the net last ran
    // Latched neural verdict. The net runs every kSpecIntervalSec but analyseNow()
    // publishes every ~700 ms, so without latching the chroma answer would overwrite
    // the (much better) neural one on ~6 of every 7 cycles. Once the net has spoken we
    // keep publishing its verdict until it speaks again.
    bool specHaveResult = false;
    int  specRoot = 0;
    bool specMinor = false;
    double specConfidence = 0.0;

    std::atomic<int>   detRoot { 0 };
    std::atomic<bool>  detMinor { false }, detValid { false }, rescan { false }, publishingNow { false };
    std::atomic<bool>  detNeural { false };        // true when the last key came from the neural model
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
