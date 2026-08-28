#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/DeEssModule.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <array>

// =============================================================================
//  Sattari De-Ess — dynamic sibilance control
// -----------------------------------------------------------------------------
//  Splits a high sibilance band out of the signal (a complementary high-pass, so
//  low + band reconstructs the input exactly) and ducks it whenever its level
//  crosses the threshold. Split mode reduces only the band (transparent); Wideband
//  mode ducks the whole signal like a broadband de-esser. Stereo-linked detection
//  keeps the image stable. Listen solos the sibilance band for easy tuning. Pure
//  reuse of the suite's EnvelopeFollower; allocation-free after prepareToPlay.
// =============================================================================
class SattariDeEssAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariDeEssAudioProcessor();
    ~SattariDeEssAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari De-Ess"; }
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

    // Live gain-reduction (dB, positive number) for the editor meter.
    float getReductionDb() const noexcept { return module.currentReductionDb(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // RBJ biquad. `hp` extracts the sibilance band for DETECTION; `shelf` is a
    // high-shelf used for PROCESSING. Split mode crossfades dry -> shelf by how
    // much sibilance is present, so with no sibilance the output is the exact dry
    // signal (truly transparent) and a shelf only cuts the highs when it fires.

    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "deess" };   // after apvts
    int currentProgram = 0;
    // Thin wrapper: the standalone plugin and the Mix rack slot run the SAME module.
    sattari::dsp::DeEssModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariDeEssAudioProcessor)
};
