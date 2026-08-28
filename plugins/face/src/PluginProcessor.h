#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/FaceModule.h>
#include <sattari_audio_core/SattariPresetManager.h>

// =============================================================================
//  Sattari Face — formant / gender / character shifter
// -----------------------------------------------------------------------------
//  Independently shifts PITCH and FORMANTS (the vocal-tract resonances that make
//  a voice sound big/small, male/female). Move formants alone for a gender/size
//  morph with the same notes; move pitch alone for a formant-corrected transpose;
//  move both for full creative transformation. Built on the suite's vendored
//  Rubber Band engine with a clean output FIFO (no crackle); dry mix is delay-
//  compensated so blends stay phase-aligned.
//
//  The shifter lives in sattari::dsp::FaceModule, shared with the Vocal rack
//  slot; this shell owns the preset bank and the host plumbing.
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
    sattari::presets::SattariPresetManager presetBank { apvts, "face" };   // after apvts
    int currentProgram = 0;

    // Thin wrapper: the standalone plugin and the Vocal rack slot run the SAME
    // module. Parameter IDs are unchanged (empty prefix), so sessions survive.
    sattari::dsp::FaceModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariFaceAudioProcessor)
};
