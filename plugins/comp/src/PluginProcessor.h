#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <atomic>

// =============================================================================
//  Sattari Comp — vocal-voiced feed-forward compressor
// -----------------------------------------------------------------------------
//  A full-featured, standalone dynamics box tuned for vocals. Signal path:
//
//    detector (stereo-max) -> sidechain high-pass one-pole  (de-emphasise lows)
//                          -> log-domain gain computer (threshold/ratio/knee)
//                          -> asymmetric attack/release smoother (EnvelopeFollower)
//                          -> gain + makeup, dry/wet (parallel) blend
//
//  The gain computer works in dB with a soft-knee interpolation, exactly the
//  classic feed-forward topology. The attack/release smoothing runs on the
//  gain-reduction envelope (in dB) using the shared suite EnvelopeFollower's
//  processFalling() primitive (fast toward MORE reduction, slow on recovery).
//
//  Extras: auto-release (program-dependent release), auto make-up gain, a
//  sidechain-listen solo toggle, and a "tempo release" mode where the release
//  time is a musical note division read off the cross-plugin TEMPO bus
//  (TempoReader) with a host-BPM (then 120) fallback. Live gain reduction is
//  published to the editor via a lock-free atomic.
//
//  Mirrors plugins/side-chain-master (detector + follower + GR meter) and
//  plugins/transient (overall plugin structure). Reuses SattariEnvelopeFollower.
// =============================================================================
class SattariCompAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariCompAudioProcessor();
    ~SattariCompAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Comp"; }
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

    // Lock-free readouts for the editor.
    float getGainReductionDb() const noexcept { return gainReductionDb.load (std::memory_order_relaxed); }
    double getEffectiveReleaseMs() const noexcept { return effectiveReleaseMs.load (std::memory_order_relaxed); }
    bool  getTempoReleaseActive() const noexcept { return tempoReleaseActive.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    bool  boolParam (const char* id) const noexcept { return param (id) > 0.5f; }
    int   choiceParam (const char* id) const noexcept { return (int) std::round (param (id)); }

    // One-pole high-pass on the detector path (de-emphasises lows before detection).
    float detectorHighPass (float input, float cutoffHz) noexcept;
    // Resolve the release time in ms, honouring the tempo-release toggle.
    double resolveReleaseMs (double userReleaseMs);
    double resolveBpm();

    juce::AudioProcessorValueTreeState apvts;
    double sampleRate = 44100.0;

    // Gain-reduction envelope smoother (dB domain). Detector is instantaneous
    // peak of the (high-passed) stereo-max, so the follower owns all the timing.
    sattari::dsp::EnvelopeFollower grSmoother;
    float smoothedGrDb = 0.0f;        // current smoothed gain reduction (<= 0 dB)
    float hpX1 = 0.0f, hpY1 = 0.0f;   // detector high-pass state

    sattari::xplugin::TempoReader tempoReader;   // cross-plugin TEMPO bus consumer

    std::atomic<float>  gainReductionDb { 0.0f };     // published GR magnitude (>= 0)
    std::atomic<double> effectiveReleaseMs { 100.0 }; // resolved release for UI readout
    std::atomic<bool>   tempoReleaseActive { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariCompAudioProcessor)
};
