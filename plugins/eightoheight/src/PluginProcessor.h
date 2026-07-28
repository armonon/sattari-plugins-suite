#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/EightOhEightModule.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariPresetManager.h>

// =============================================================================
//  Sattari 808 — audio-follow 808 / sub-bass designer (an EFFECT with audio in).
// -----------------------------------------------------------------------------
//  Tracks the fundamental of an incoming bass/vocal with the shared YIN pitch
//  detector, snaps that pitch to the KEY-bus scale (when engaged), and drives a
//  tuned sine/triangle 808 body oscillator under it. The body has portamento
//  glide, a retriggering amp-decay envelope, a drive/click transient, and a
//  sub-harmonic fold, blended against the dry input. Supersets Sub Conjurer:
//  same audio-follow oscillator + envelope spine, but with an explicit 808 body
//  (attack click + amp decay + one-octave-down sub fold) and key-snapped tuning.
// =============================================================================

class Sattari808AudioProcessor final : public juce::AudioProcessor
{
public:
    Sattari808AudioProcessor();
    ~Sattari808AudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}   // the module stops its own bus reader

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
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

    // Lock-free readout for the editor: the current 808 body frequency in Hz.
    float currentBodyHz() const noexcept { return module.currentBodyHz(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "eightoheight" };   // after apvts
    int currentProgram = 0;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:

    // Thin wrapper: the standalone plugin and the Create rack slot run the SAME
    // module. Parameter IDs are unchanged (empty prefix), so sessions survive.
    sattari::dsp::EightOhEightModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sattari808AudioProcessor)
};
