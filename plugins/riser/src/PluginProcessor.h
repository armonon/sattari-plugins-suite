#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <atomic>
#include <vector>

// =============================================================================
//  Sattari Riser — tempo-synced riser / downlifter / impact FX
// -----------------------------------------------------------------------------
//  Arm the effect (a UI toggle, or optionally the host's transport play-start)
//  and it synthesises a build of a chosen bar length that resolves on the next
//  downbeat: a rising filtered-noise sweep + an opening resonant filter + a
//  pitch-rise on the input + an accelerating stutter, mixed with (or replacing)
//  the dry signal.
//
//  A progress counter runs 0->1 over `length` bars. Bars->samples is derived
//  from the resolved BPM (host playhead first, then the cross-plugin TEMPO bus,
//  then 120). The counter advances per sample so the build is sample-accurate.
//
//  Modes:
//    Riser      — amplitude + filter + pitch all rise; lands loud on the beat.
//    Downlifter — inverted: everything falls into the beat (the classic "suck").
//    Impact     — a short build that snaps to a broadband hit + tail on landing.
//
//  RT-safe: everything is preallocated in prepareToPlay; processBlock does no
//  allocation and no locking; the output is soft-ceilinged so it can never blow
//  up. UI reads a lock-free atomic for the build progress + armed state.
// =============================================================================
class SattariRiserAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariRiserAudioProcessor();
    ~SattariRiserAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Riser"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

    static juce::StringArray modeChoices();
    static juce::StringArray lengthChoices();

    // Lock-free UI readouts.
    double currentBpm()   const noexcept { return lastBpm.load (std::memory_order_relaxed); }
    float  buildProgress() const noexcept { return progressUi.load (std::memory_order_relaxed); }
    bool   isBuilding()    const noexcept { return buildingUi.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    double resolveBpm();

    juce::AudioProcessorValueTreeState apvts;
    double sampleRate = 44100.0;

    sattari::xplugin::TempoReader tempoReader;   // TEMPO-bus fallback when host reports none
    std::atomic<double> lastBpm { 120.0 };

    // --- Build engine state (audio thread only) ------------------------------
    bool   building = false;    // a build is currently in flight
    double progress = 0.0;      // 0..1 over the chosen bar length
    double progressPerSample = 0.0;
    bool   prevArm = false;     // edge-detect the arm toggle
    bool   prevPlaying = false; // edge-detect host transport start (host-trigger mode)

    // Filtered-noise sweep.
    juce::dsp::StateVariableTPTFilter<float> noiseFilter;
    juce::Random rng;

    // Tone oscillator (rising sine for the "tonal" half of the build).
    double tonephase = 0.0;

    // Pitch-rise on the input: a short delay line read back at a varying rate.
    std::vector<float> pitchBuf[2];
    int    pitchLen = 0;
    int    pitchWriteI = 0;
    double pitchRead[2] { 0.0, 0.0 };

    // Accelerating stutter (a re-triggered capture-and-hold gate).
    std::vector<float> stutBuf[2];
    int    stutLen = 0;
    int    stutWrite = 0;
    int    stutGrainSamples = 0;     // current grain length (shrinks as build accelerates)
    int    stutPos = 0;              // playback position within the current grain

    // Output soft ceiling smoother state (per channel) + a landing "impact" tail.
    float  impactEnv = 0.0f;         // decays after an Impact landing
    float  impactPhase = 0.0f;

    std::atomic<float> progressUi { 0.0f };
    std::atomic<bool>  buildingUi { false };

    void startBuild (double bpm, int lengthBars);
    void finishBuild();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariRiserAudioProcessor)
};
