#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/DoubleModule.h>
#include <sattari_audio_core/SattariPresetManager.h>

// =============================================================================
//  Sattari Double — natural vocal doubler / thickener
// -----------------------------------------------------------------------------
//  Generates two decorrelated "doubles" of the input and pans them left/right
//  around the dry centre, the way a singer tracking a part twice would widen and
//  thicken. Each double is (1) pitch-detuned a few cents with the shared
//  formant-preserving PSOLA shifter so it never sounds like a chorus whistle,
//  (2) time-offset a few ms, and (3) slowly drifted so the two takes breathe
//  independently. Pure reuse of the suite's PSOLA + YIN engines; allocation-free
//  after prepareToPlay.
// =============================================================================
class SattariDoubleAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariDoubleAudioProcessor();
    ~SattariDoubleAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Double"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "double" };   // after apvts
    int currentProgram = 0;

    // Thin wrapper: the standalone plugin and the Vocal rack slot run the SAME
    // module. Parameter IDs are unchanged (empty prefix), so sessions survive.
    sattari::dsp::DoubleModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariDoubleAudioProcessor)
};
