#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <atomic>

// =============================================================================
//  Sattari Pulse — tempo-locked creative pump / gate
// -----------------------------------------------------------------------------
//  A "sidechain-pump without routing". A per-beat volume-shaping curve is applied
//  as a rhythmic ducking / gating envelope on the incoming audio: a tempo phase
//  (0..1 per chosen note-division of the resolved BPM) indexes a SELECTABLE curve
//  shape (Sine / Saw-down / Saw-up / Square / Spike) to produce a 0..1 gain each
//  sample. The gain is smoothed, multiplied into the input, and can optionally
//  drive a one-pole low-pass whose cutoff moves with the same curve.
//
//  Locks to the host tempo while playing; when the host reports no tempo
//  (standalone / transport-less host) it free-runs on the cross-plugin tempo bus,
//  then on 120 BPM. Allocation-free after prepareToPlay; output is bounded so it
//  can only ever duck the signal (0 .. input) and never exceed it.
// =============================================================================
class SattariPulseAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariPulseAudioProcessor();
    ~SattariPulseAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Pulse"; }
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

    static juce::StringArray rateChoices();
    static juce::StringArray shapeChoices();

    double currentBpm() const noexcept { return lastBpm.load (std::memory_order_relaxed); }
    // 0..1 modulation gain currently applied (for the UI meter/indicator).
    float  currentGain() const noexcept { return lastGain.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    double resolveBpm();

    // Curve lookup: phase in [0,1) -> ducking gain in [0,1]. 1 = untouched,
    // 0 = fully ducked at the beat. `shape` selects the family.
    static float curveGain (int shape, float phase) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    double sampleRate = 44100.0;

    sattari::xplugin::TempoReader tempoReader;   // consumes the cross-plugin tempo bus

    // Tempo phase: free-runs from BPM, re-anchored to host PPQ each block while playing.
    double phase = 0.0;

    // Per-sample gain smoother (one-pole) — kills zipper/step artifacts, esp. Square/Spike.
    float smoothedGain = 1.0f;
    float smoothCoeff  = 0.0f;   // set per-block from the Smooth control

    // Optional filter movement: one-pole low-pass modulated by the same curve.
    float lpStateL = 0.0f, lpStateR = 0.0f;

    std::atomic<double> lastBpm  { 120.0 };
    std::atomic<float>  lastGain { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariPulseAudioProcessor)
};
