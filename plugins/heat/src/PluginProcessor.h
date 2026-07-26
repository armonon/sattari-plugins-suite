#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/HeatModule.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <array>
#include <atomic>
#include <memory>

// =============================================================================
//  Sattari Heat — saturation / tone box
// -----------------------------------------------------------------------------
//  A standalone character saturator with four voicings (Tube / Tape /
//  Transformer / Transistor). Each is a distinct waveshaper run inside a 4x
//  oversampler so the harmonics it generates fold far less aliasing back into
//  the audible band; the reported latency is the oversampler's, and the dry
//  parallel-mix path is delay-compensated to match so wet/dry stay phase
//  aligned. A tone-tilt shelf pair sits pre-shaper (dark <-> bright), a
//  bias/asymmetry control skews the curve to add even harmonics (DC-blocked
//  after), and a soft ceiling guarantees the output can never blow up.
//
//  Mirrors Sattari Bus for the oversampled + latency-comped drive plumbing and
//  Royal Chain's ColorModule for the shaper mindset (drive compensation, mix).
// =============================================================================
class SattariHeatAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariHeatAudioProcessor();
    ~SattariHeatAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Heat"; }
    bool acceptsMidi() const override { return true; }   // for MIDI Learn CC input
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }
    // Post-shaper drive read-out (0..1) for a UI heat meter.
    float getDriveMeter() const noexcept { return module.currentDriveMeter(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float getParam (const char* id) const noexcept;
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:
    int currentProgram = 0;
    double sampleRate = 44100.0;

    // Thin wrapper: the standalone plugin and the Mix rack slot run the SAME module.
    sattari::dsp::HeatModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariHeatAudioProcessor)
};
