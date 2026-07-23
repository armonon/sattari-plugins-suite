#pragma once

#include <JuceHeader.h>

// =============================================================================
//  Sattari Chop — slicing sampler.
// -----------------------------------------------------------------------------
//  Loads source audio (file / stem / deck capture), detects onsets into a slice
//  table (hand-editable, with locked markers that survive re-detection), maps
//  slices to pads/keys, and plays them back with per-slice processing. This is
//  the scaffold: the plugin builds + validates as an instrument; the slicing
//  engine, mapping, voices, and the waveform editing surface land on top.
//
//  See CHOP_SPEC (§0 build order): the editing surface is the product, and its
//  feel is verified in a DAW — the engine is verified offline on real material.
// =============================================================================
class SattariChopAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariChopAudioProcessor();
    ~SattariChopAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Chop"; }
    bool acceptsMidi() const override { return true; }
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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariChopAudioProcessor)
};
