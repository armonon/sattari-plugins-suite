#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/WarpModule.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariPresetManager.h>

// =============================================================================
//  Sattari Warp — creative vocal transformer
// -----------------------------------------------------------------------------
//  A destructive-in-a-good-way vocal FX unit. One big DOSE macro escalates a
//  chain of real DSP blocks from "subtle character" to "fully mangled":
//
//    1. VOWEL   — morphable formant filter (two bandpass resonators sweeping
//                 A-E-I-O-U). Talkbox / vocal-tract warp.
//    2. ROBOT   — ring modulation against a tunable carrier. Metallic / robotic.
//    3. GRIT    — waveshaper drive + bit / sample-rate crush. Dirt and lo-fi.
//    4. STUTTER — beat-repeat: captures a slice and loops it (tempo-synced when
//                 the host gives a tempo). Glitch / stutter textures.
//
//  DOSE scales every block's intensity together so one knob takes you deeper.
//  Everything is allocation-free after prepareToPlay. Pure JUCE + std DSP; the
//  block math is unit-tested in tests/warp_contract.cpp.
// =============================================================================
class SattariWarpAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariWarpAudioProcessor();
    ~SattariWarpAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Warp"; }
    bool acceptsMidi() const override { return true; }   // for MIDI Learn
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

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

    // Live output level (for the editor's reactive visual). 0..~1.
    float getOutputLevel() const noexcept { return module.currentOutLevel(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "warp" };   // after apvts
    int currentProgram = 0;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:

    // Thin wrapper: the standalone plugin and the Create rack slot run the SAME
    // module. Parameter IDs are unchanged (empty prefix), so sessions survive.
    sattari::dsp::WarpModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariWarpAudioProcessor)
};
