#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// Sattari Align - time-aligns the Main signal to the Sidechain ("Guide") signal
// using onset-envelope cross-correlation. Works on anything with transients:
// vocal doubles, guitar layers, drum stems, stereo pairs, master vs mix-bus
// comparison, etc.
//
// Algorithm (v1, bulk delay only):
//   1. Compute a fast onset envelope on both signals (rectified differentiated
//      energy, decimated to 200 Hz).
//   2. Every ~200 ms, normalised cross-correlation between recent envelopes
//      gives the integer envelope-lag that best aligns the two.
//   3. Convert lag to samples and apply as a smoothed delay on the Main output.
//
// We always delay Main by `baseLatencySamples` + smoothedLagSamples so the lag
// can swing both negative and positive without going below zero. The plugin
// reports `baseLatencySamples` as host latency, so DAW delay-comp keeps the
// project tight. v2 territory (NOT in this build): DTW with anchor warping
// instead of bulk shift; multi-channel-aware alignment; pitch matching.
class SattariAlignAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariAlignAudioProcessor();
    ~SattariAlignAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Align"; }
    bool acceptsMidi() const override { return true; }   // for MIDI Learn
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

    // UI snapshot (lock-free POD; safe to read off the audio thread).
    struct LiveSnapshot
    {
        float detectedOffsetMs = 0.0f;   // smoothed lag applied to Main (signed)
        float confidence = 0.0f;         // peak correlation in [0, 1]
        bool  guideActive = false;       // true when SC bus delivered audio
        bool  locked = false;            // true when the lag is held (manual or auto-lock)
    };
    LiveSnapshot getLiveSnapshot() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOnsetEnvelope (const juce::AudioBuffer<float>& mainBuf,
                              const juce::AudioBuffer<float>& guideBuf,
                              int numSamples);
    int  searchBestLagFrames (float& outConfidence) const;
    void resetDspState();

    juce::AudioProcessorValueTreeState apvts;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:
    double currentSampleRate = 44100.0;
    int    preparedBlockSize = 512;

    // --- Onset envelopes (200 Hz internal rate) ------------------------------
    static constexpr int    envSampleRate  = 200;
    static constexpr int    envHistorySize = envSampleRate * 6;   // 6 seconds
    static constexpr int    envWindowSize  = envSampleRate * 3;   // 3-second cross-corr window
    std::vector<float> mainEnv, guideEnv;                         // rolling rings
    int    envWritePos = 0;
    int    envSamplesPerHost = 1;
    int    envAccumulator = 0;
    float  mainPrevEnergy = 0.0f, guidePrevEnergy = 0.0f;
    float  mainEnvAccum = 0.0f, guideEnvAccum = 0.0f;
    int    mainEnvCount = 0, guideEnvCount = 0;
    sattari::dsp::EnvelopeFollower mainSmoother, guideSmoother;

    // --- Delay line on the Main path -----------------------------------------
    int baseLatencySamples = 0;        // = ceil(searchRange seconds * sr); reported to host
    int maxDelaySamples = 0;
    std::vector<float> delayLineL, delayLineR;
    int delayWritePos = 0;
    float smoothedLagSamples = 0.0f;   // signed; positive = main delayed more

    // --- DTW warp state (v2 alpha) -------------------------------------------
    // A piecewise-linear warp path: lagSamples = warpLagAt(t). Computed every
    // search cycle from a constrained-band DTW between the recent Guide and
    // Main envelopes. The audio loop interpolates this path each block.
    struct WarpAnchor { double timeSec; float lagSamples; };
    std::vector<WarpAnchor> warpPath;
    juce::CriticalSection warpPathLock;
    double dtwClockSec = 0.0;       // monotonic audio-thread clock (s)
    int    computeDtwWarp();        // returns 0 if path updated, non-zero on skip

    // Preallocated DTW scratch (sized in prepareToPlay) so computeDtwWarp() never
    // allocates on the audio thread. cost is the flat (i,j) cost matrix; rev holds
    // the backtracked anchors before they are reversed into warpPath.
    std::vector<double>     dtwCost;
    std::vector<WarpAnchor> dtwRev;

    // --- Detection cadence + output state ------------------------------------
    int samplesUntilSearch = 0;
    std::atomic<float> publishedOffsetMs { 0.0f };
    std::atomic<float> publishedConfidence { 0.0f };
    std::atomic<bool>  guideHasAudio { false };
    std::atomic<bool>  locked { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariAlignAudioProcessor)
};
