#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include "RbFormant.h"
#include <array>
#include <vector>

// =============================================================================
//  Sattari Face — formant / gender / character shifter
// -----------------------------------------------------------------------------
//  Independently shifts PITCH and FORMANTS (the vocal-tract resonances that make
//  a voice sound big/small, male/female). Move formants alone for a gender/size
//  morph with the same notes; move pitch alone for a formant-corrected transpose;
//  move both for full creative transformation. Built on the suite's vendored
//  Rubber Band engine with a clean output FIFO (no crackle); dry mix is delay-
//  compensated so blends stay phase-aligned.
// =============================================================================
class SattariFaceAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariFaceAudioProcessor();
    ~SattariFaceAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Face"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }

    juce::AudioProcessorValueTreeState apvts;
    double sampleRate = 44100.0;

    std::array<sattari::face::RbFormant, 2> rb;
    std::vector<float> wetScratch;

    // Dry delay so a dry/wet blend stays phase-aligned with the RB latency.
    std::array<std::vector<float>, 2> dryDelay;
    int dryWrite = 0, reportedLatency = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariFaceAudioProcessor)
};
