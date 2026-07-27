#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/RiserModule.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariPresetManager.h>

// =============================================================================
//  Sattari Riser — tempo-synced riser / downlifter / impact FX
// -----------------------------------------------------------------------------
//  Arm the effect (a UI toggle, or optionally the host's transport play-start)
//  and it synthesises a build of a chosen bar length that resolves on the next
//  downbeat: a rising filtered-noise sweep + an opening resonant filter + a
//  pitch-rise on the input + an accelerating stutter, mixed with (or replacing)
//  the dry signal.
//
//  A progress counter runs 0->1 over `length` bars. Bars->samples is derived
//  from the resolved BPM (host playhead first, then the cross-plugin TEMPO bus,
//  then 120). The counter advances per sample so the build is sample-accurate.
//
//  Modes:
//    Riser      — amplitude + filter + pitch all rise; lands loud on the beat.
//    Downlifter — inverted: everything falls into the beat (the classic "suck").
//    Impact     — a short build that snaps to a broadband hit + tail on landing.
//
//  RT-safe: everything is preallocated in prepareToPlay; processBlock does no
//  allocation and no locking; the output is soft-ceilinged so it can never blow
//  up. UI reads a lock-free atomic for the build progress + armed state.
// =============================================================================
class SattariRiserAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariRiserAudioProcessor();
    ~SattariRiserAudioProcessor() override = default;   // module stops its own tempo reader

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Riser"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

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

    static juce::StringArray modeChoices()   { return sattari::dsp::RiserModule::modeChoices(); }
    static juce::StringArray lengthChoices() { return sattari::dsp::RiserModule::lengthChoices(); }

    // Lock-free UI readouts.
    double currentBpm()    const noexcept { return module.currentBpm(); }
    float  buildProgress() const noexcept { return module.buildProgress(); }
    bool   isBuilding()    const noexcept { return module.isBuilding(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "riser" };   // after apvts
    int currentProgram = 0;

    // Thin wrapper: the standalone plugin and the Create rack slot run the SAME
    // module. Parameter IDs are unchanged (empty prefix), so sessions survive.
    sattari::dsp::RiserModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariRiserAudioProcessor)
};
