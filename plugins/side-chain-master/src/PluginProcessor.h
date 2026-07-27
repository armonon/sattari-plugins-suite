#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/SideChainMasterModule.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariMidiLearn.h>

class SideChainMasterAudioProcessor final : public juce::AudioProcessor
{
public:
    SideChainMasterAudioProcessor();
    ~SideChainMasterAudioProcessor() override = default;   // module stops its own tempo reader

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }   // for MIDI Learn CC input
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
    const juce::AudioProcessorValueTreeState& parameters() const { return apvts; }

    float getLastGainReductionDb() const { return module.currentGainReductionDb(); }
    bool getLastExternalSidechainAvailable() const { return module.externalKeyInUse(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:

    // Thin wrapper: the standalone plugin and the Mix rack slot run the SAME module.
    // Parameter IDs are unchanged (empty prefix), so sessions and presets survive.
    sattari::dsp::SideChainMasterModule module;
    int reportedLatency = -1;   // last value published to the host (-1 forces a first publish)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SideChainMasterAudioProcessor)
};
