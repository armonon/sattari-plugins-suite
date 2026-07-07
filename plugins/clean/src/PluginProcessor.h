#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <array>
#include <atomic>

// =============================================================================
//  Sattari Clean — adaptive multiband denoiser
// -----------------------------------------------------------------------------
//  Splits the signal into three bands with Linkwitz-Riley crossovers and runs a
//  downward expander in each. Every band learns its own noise floor (a slow
//  minimum follower), so bands that contain only noise — hiss up top, hum/rumble
//  down low, room in the mids — are pushed down while real signal above the floor
//  passes. Amount sets depth, Sensitivity how far above the floor it opens.
//  Stereo-linked so the image stays put; reuses the suite EnvelopeFollower.
// =============================================================================
class SattariCleanAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariCleanAudioProcessor();
    ~SattariCleanAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Clean"; }
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
    float getReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }

private:
    static constexpr int kBands = 3;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }

    juce::AudioProcessorValueTreeState apvts;
    double sampleRate = 44100.0;

    juce::dsp::LinkwitzRileyFilter<float> lr1, lr2;   // crossovers (low|rest, mid|high)
    std::array<sattari::dsp::EnvelopeFollower, kBands> env;   // stereo-linked, per band
    std::array<float, kBands> floor { { 1.0f, 1.0f, 1.0f } };   // min-follower: starts high, drops to the noise floor
    std::array<float, kBands> gSmooth { { 1.0f, 1.0f, 1.0f } };
    std::atomic<float> reductionDb { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariCleanAudioProcessor)
};
