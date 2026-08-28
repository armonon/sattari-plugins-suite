#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/SpaceModule.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariPresetManager.h>

// =============================================================================
//  Sattari Space — reverb + tempo-synced delay
// -----------------------------------------------------------------------------
//  Fills the suite's time-FX gap. A Freeverb-based reverb (size / damping /
//  width / mix) fed by a tempo-synced stereo delay (note-division time, damped
//  feedback, mix). The delay locks to the host tempo; when the host reports no
//  tempo (standalone, or a transport-less host) it falls back to the Sattari
//  session bus tempo — the consumer side of the cross-plugin tempo bus — then to
//  120 BPM.
//
//  The reverb itself lives in sattari::dsp::SpaceModule, shared with the Create
//  rack slot; this shell owns the preset bank and the host plumbing.
// =============================================================================
class SattariSpaceAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariSpaceAudioProcessor();
    ~SattariSpaceAudioProcessor() override = default;   // module stops its own tempo reader

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Space"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

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
    static juce::StringArray divisionChoices() { return sattari::dsp::SpaceModule::divisionChoices(); }
    double currentBpm() const noexcept { return module.currentBpm(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "space" };   // after apvts
    int currentProgram = 0;

    // Thin wrapper: the standalone plugin and the Create rack slot run the SAME
    // module. Parameter IDs are unchanged (empty prefix), so sessions survive.
    sattari::dsp::SpaceModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariSpaceAudioProcessor)
};
