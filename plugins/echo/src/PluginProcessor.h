#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/EchoModule.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariPresetManager.h>

// =============================================================================
//  Sattari Echo — tempo-locked rhythmic / ping-pong delay
// -----------------------------------------------------------------------------
//  A stereo delay whose time locks to the host tempo (else the cross-plugin
//  TempoReader bus, else 120 BPM), either as a note division (sync) or a free
//  millisecond time. The feedback path runs through a one-pole tone/damping
//  filter and optional cross-feed for the classic ping-pong bounce. A
//  tape-smear diffusion all-pass loosens the repeats, and an input-driven
//  envelope follower ducks the wet under the dry so the delays stay out of the
//  way of the source. Fully preallocated in prepare(); allocation-free and
//  bounded on the audio thread.
//
//  The delay itself lives in sattari::dsp::EchoModule, shared with the Create
//  rack slot; this shell owns the preset bank and the host plumbing.
// =============================================================================
class SattariEchoAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariEchoAudioProcessor();
    ~SattariEchoAudioProcessor() override = default;   // module stops its own tempo reader

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Echo"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

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
    static juce::StringArray divisionChoices() { return sattari::dsp::EchoModule::divisionChoices(); }
    double currentBpm() const noexcept { return module.currentBpm(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "echo" };   // after apvts
    int currentProgram = 0;

    // Thin wrapper: the standalone plugin and the Create rack slot run the SAME
    // module. Parameter IDs are unchanged (empty prefix), so sessions survive.
    sattari::dsp::EchoModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariEchoAudioProcessor)
};
