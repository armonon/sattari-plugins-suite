#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/ShimmerModule.h>
#include <sattari_audio_core/SattariPresetManager.h>

// =============================================================================
//  Sattari Shimmer — pitch-shifted "shimmer" reverb
// -----------------------------------------------------------------------------
//  A reverb tank whose feedback path contains a pitch shifter, so the tail
//  blooms upward into a crystalline pad. Signal flow per block:
//
//    dry --> [pre-delay] --> tank input += shimmer * shifted(prev wet)
//                              |
//                           juce::Reverb  (taken as PURE wet, dryLevel 0)
//                              |
//                            wet --> [tone/damp one-pole LP] --> PSOLA(+interval)
//                                        (feeds back into next block's input)
//                              |
//              out = dry*(1-mix) + wet*mix     (our OWN mix, mix=0 = pass-through)
//
//  The shimmer amount scales the fed-back shifted signal; because it re-enters a
//  decaying tank at amount < 1 and is low-passed each pass, the loop is bounded
//  (energy shed by the reverb decay + damping exceeds the shimmer re-injection).
//
//  Key Snap (optional): reads the Sattari KEY bus (SessionReader) via the shared
//  KeyButton, and quantizes the +12/+7/+5 shimmer interval so the transposed
//  pitch class lands on an in-key scale tone — the bloom never clashes with the
//  song. Allocation-free after prepareToPlay; UI reads lock-free atomics only.
// =============================================================================
class SattariShimmerAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariShimmerAudioProcessor();
    ~SattariShimmerAudioProcessor() override = default;   // module stops its own bus reader

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Shimmer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

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
    static juce::StringArray intervalChoices() { return sattari::dsp::ShimmerModule::intervalChoices(); }

    // Effective key state for the editor's KeyButton (whether Key Snap is on and
    // the key it is following). rootChoices / scaleChoices mirror the KEY bus.
    static juce::StringArray rootChoices()  { return sattari::dsp::ShimmerModule::rootChoices(); }
    static juce::StringArray scaleChoices() { return sattari::dsp::ShimmerModule::scaleChoices(); }
    using KeyState = sattari::dsp::ShimmerModule::KeyState;
    KeyState currentKeyState() const { return module.currentKeyState(); }

    // Exposed for the contract: the effective (possibly key-snapped) shimmer
    // interval in semitones for a given base interval + key.
    int effectiveShimmerSemis (int baseSemis, int keyRoot, sattari::dsp::ScaleId id) const
    { return module.effectiveShimmerSemis (baseSemis, keyRoot, id); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "shimmer" };   // after apvts
    int currentProgram = 0;

    // Thin wrapper: the standalone plugin and the Create rack slot run the SAME
    // module. Parameter IDs are unchanged (empty prefix), so sessions survive.
    sattari::dsp::ShimmerModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariShimmerAudioProcessor)
};
