#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/WarbleModule.h>
#include <sattari_audio_core/SattariPresetManager.h>

// =============================================================================
//  Sattari Warble — tape / vinyl lo-fi character box
// -----------------------------------------------------------------------------
//  A single "Age" macro drives a chain of tape/vinyl degradation:
//    * Wow & flutter  — a modulated fractional delay line (slow wow LFO + a
//      faster flutter LFO) produces pitch drift/wobble.
//    * Tape saturation — a tanh drive stage softens and compresses peaks.
//    * Head-bump / tone — a low-shelf bump plus a gentle high-cut for a warm,
//      band-limited "old-machine" tone.
//    * Hiss           — filtered low-level noise floor.
//    * Crackle        — sparse impulsive vinyl pops (optional).
//  "Age" scales wow depth, flutter depth, saturation drive, hiss and crackle
//  amounts together; each also has its own trim. The plugin keeps its own
//  dry/wet mix so mix=0 (or Age=0 with the mix-scaled effects) is a clean
//  pass-through. Allocation-free after prepareToPlay, RT-safe, bounded output.
// =============================================================================
class SattariWarbleAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariWarbleAudioProcessor();
    ~SattariWarbleAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Warble"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

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
    sattari::presets::SattariPresetManager presetBank { apvts, "warble" };   // after apvts
    int currentProgram = 0;

    // Thin wrapper: the standalone plugin and the Create rack slot run the SAME
    // module. Parameter IDs are unchanged (empty prefix), so sessions survive.
    sattari::dsp::WarbleModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariWarbleAudioProcessor)
};
