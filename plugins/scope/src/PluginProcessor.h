#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariChromagram.h>
#include <sattari_audio_core/SattariKeyDetector.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariRealtime.h>
#include <array>
#include <atomic>
#include <thread>
#include <vector>

// =============================================================================
//  Sattari Scope — pass-through visual analyzer
// -----------------------------------------------------------------------------
//  A transparent analyser: audio is NEVER altered (bit-exact pass-through). It
//  publishes, lock-free, three views for the editor to draw:
//    * a log-spaced FFT magnitude spectrum (dB), computed on a background worker
//      thread from a windowed Hann FFT (mirrors Sattari Key's chroma FFT),
//    * a goniometer / vectorscope point cloud + L/R correlation, reduced on the
//      audio thread (like Sattari Meter's correlation),
//    * an optional pitch-class (chroma) overlay tying spectrum energy to the key
//      currently on the cross-plugin bus.
//  The audio thread only writes into a ring and small atomics; the heavy FFT
//  runs off-thread, so processBlock never blocks or allocates.
// =============================================================================
class SattariScopeAudioProcessor final : public juce::AudioProcessor
{
public:
    // Fixed-size snapshot geometry (keeps the published POD trivially copyable).
    static constexpr int kNumBands   = 128;  // log-spaced spectrum display bins
    static constexpr int kNumGonio   = 256;  // goniometer sample points (decimated)
    static constexpr int kFftOrder   = 12;   // 4096-point FFT
    static constexpr int kFftSize    = 1 << kFftOrder;

    struct Spectrum
    {
        float bandDb[kNumBands];   // -100..0 dB per log-spaced band
        float chroma[12];          // normalised 0..1 pitch-class energy
        int   keyRoot   = -1;      // 0..11 from the bus, -1 if none
        int   keyScale  = 0;       // 0 major, 1 minor
        bool  keyValid  = false;
        bool  hasData   = false;
    };

    struct Vector
    {
        // Mid/side pairs for the goniometer, already rotated 45deg (x=side, y=mid),
        // normalised to roughly [-1,1]. gonioCount points are valid.
        float x[kNumGonio];
        float y[kNumGonio];
        int   gonioCount = 0;
        float correlation = 0.0f;  // -1..+1
    };

    SattariScopeAudioProcessor();
    ~SattariScopeAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Scope"; }
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

    // Lock-free reads for the editor timer.
    Spectrum getSpectrum() const noexcept { return spectrumSnap.load(); }
    Vector   getVector()   const noexcept { return vectorSnap.load(); }

    // Analysis entry point used by the worker thread AND the contract test (so a
    // test can force a synchronous analysis pass without racing the worker).
    void analyseNow();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const juce::String& id) const noexcept
    {
        if (auto* p = apvts.getRawParameterValue (id)) return p->load();
        return 0.0f;
    }

    void workerLoop();

    juce::AudioProcessorValueTreeState apvts;
    double sampleRate = 44100.0;

    // --- analysis ring (audio thread writes, worker reads) -------------------
    std::vector<float> ring;                   // kFftSize mono history
    std::atomic<int>   writePos { 0 };

    // --- worker-owned FFT scratch (never touched by audio thread) ------------
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> fftScratch;             // kFftSize * 2
    std::vector<float> window;                 // Hann, kFftSize
    double             runningChroma[12] = { 0.0 };

    // Maps each log band to a [loBin, hiBin] range of the FFT, built in prepare.
    std::array<int, kNumBands + 1> bandEdges {};

    // --- goniometer / correlation (audio thread) -----------------------------
    std::array<float, kNumGonio> gonioX {}, gonioY {};
    int    gonioWrite = 0;
    int    gonioDecim = 1, gonioPhase = 0;     // keep ~kNumGonio points per publish
    double corrLL = 0.0, corrRR = 0.0, corrLR = 0.0;
    int    corrCount = 0, corrWindow = 0;
    float  lastCorr = 0.0f;

    // --- lock-free publish ----------------------------------------------------
    sattari::rt::AtomicSnapshot<Spectrum> spectrumSnap;
    sattari::rt::AtomicSnapshot<Vector>   vectorSnap;

    // --- background worker -----------------------------------------------------
    std::thread       worker;
    std::atomic<bool> workerRunning { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariScopeAudioProcessor)
};
