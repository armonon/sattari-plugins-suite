#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/AlignModule.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariPresetManager.h>

// Sattari Align - time-aligns the Main signal to the Sidechain ("Guide") signal
// using onset-envelope cross-correlation. Works on anything with transients:
// vocal doubles, guitar layers, drum stems, stereo pairs, master vs mix-bus
// comparison, etc.
//
// The alignment itself lives in sattari::dsp::AlignModule, shared with the Mix
// rack slot; this shell owns the Guide bus, MIDI Learn and the preset bank.
class SattariAlignAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariAlignAudioProcessor();
    ~SattariAlignAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Align"; }
    bool acceptsMidi() const override { return true; }   // for MIDI Learn
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

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

    // UI snapshot (lock-free POD; safe to read off the audio thread).
    using LiveSnapshot = sattari::dsp::AlignModule::LiveSnapshot;
    LiveSnapshot getLiveSnapshot() const noexcept { return module.getLiveSnapshot(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "align" };   // after apvts
    int currentProgram = 0;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:

    // Thin wrapper: the standalone plugin and the Mix rack slot run the SAME module.
    // Parameter IDs are unchanged (empty prefix), so sessions and presets survive.
    sattari::dsp::AlignModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariAlignAudioProcessor)
};
