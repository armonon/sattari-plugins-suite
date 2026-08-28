#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <sattari_audio_core/modules/CleanModule.h>
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
    void reset() override;
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

    int getNumPrograms() override { return juce::jmax (1, presetBank.numFactoryPresets()); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int i) override
    {
        if (i >= 0 && i < presetBank.numFactoryPresets()) { currentProgram = i; presetBank.loadFactoryPreset (i); }
    }
    const juce::String getProgramName (int i) override { return presetBank.factoryPresetNames()[i]; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }
    float getReductionDb() const noexcept { return module.getReductionDb(); }

private:
    static constexpr int kBands = 3;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "clean" };   // after apvts
    int currentProgram = 0;
    double sampleRate = 44100.0;

    // Thin wrapper: the standalone plugin and the Mix rack slot run the SAME module.
    sattari::dsp::CleanModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariCleanAudioProcessor)
};
