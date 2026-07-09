#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <array>
#include <vector>

// =============================================================================
//  Sattari Echo — tempo-locked rhythmic / ping-pong delay
// -----------------------------------------------------------------------------
//  A stereo delay whose time locks to the host tempo (else the cross-plugin
//  TempoReader bus, else 120 BPM), either as a note division (sync) or a free
//  millisecond time. The feedback path runs through a one-pole tone/damping
//  filter and optional cross-feed for the classic ping-pong bounce. A
//  tape-smear diffusion all-pass loosens the repeats, and an input-driven
//  envelope follower ducks the wet under the dry so the delays stay out of the
//  way of the source. Fully preallocated in prepareToPlay; allocation-free and
//  bounded on the audio thread.
// =============================================================================
class SattariEchoAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariEchoAudioProcessor();
    ~SattariEchoAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Echo"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }
    static juce::StringArray divisionChoices();
    double currentBpm() const noexcept { return lastBpm.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    double resolveBpm();
    double resolveDelaySamples();   // sync division or free ms -> samples (bounded)

    juce::AudioProcessorValueTreeState apvts;
    double sampleRate = 44100.0;

    sattari::xplugin::TempoReader tempoReader;   // consumes the cross-plugin tempo bus
    std::atomic<double> lastBpm { 120.0 };

    // Stereo delay lines (preallocated to a max time in prepareToPlay).
    std::array<std::vector<float>, 2> delayBuf;
    int delayLen = 0, delayWrite = 0;

    // Smoothed delay time so retimes/division changes glide instead of clicking.
    double smoothedDelaySamples = 0.0;

    // Per-channel feedback-loop damping (one-pole LP/HP blend) state.
    std::array<float, 2> dampLpState { { 0.0f, 0.0f } };

    // Per-channel diffusion all-pass (single stage, short fixed delay) for tape smear.
    std::array<std::vector<float>, 2> apBuf;
    int apLen = 0;
    std::array<int, 2> apPos { { 0, 0 } };

    // Input-driven ducking follower (drives the wet down under the dry).
    sattari::dsp::EnvelopeFollower duckFollower;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariEchoAudioProcessor)
};
