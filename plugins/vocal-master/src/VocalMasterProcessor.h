#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariStemSeparator.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "VocalMasterDsp.h"
#include <array>
#include <atomic>

// =============================================================================
//  Sattari Vocal Master — vocal cleanup + master (with AI stem-removal seam)
// -----------------------------------------------------------------------------
//  A Royal-Chain-sibling finishing chain aimed at *cleaning up* a vocal: pull
//  it out of the music/bleed, de-noise, de-ess, shape the tone, level, and
//  limit. The realtime "Isolate" stage is a mid-side music-reducer (the always-
//  on stand-in for AI separation); the same SattariStemSeparator seam StemDeck
//  uses is held here too, so the offline AI-vocal-isolation path drops in once
//  an ONNX model is present (off by default, build unchanged).
// =============================================================================
class SattariVocalMasterAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariVocalMasterAudioProcessor();
    ~SattariVocalMasterAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Vocal Master"; }
    bool acceptsMidi() const override { return true; }   // MIDI Learn
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

    float getInputMeter()  const noexcept { return inputMeter.load(); }
    float getOutputMeter() const noexcept { return outputMeter.load(); }
    float getDeEssMeter()  const noexcept { return deEssMeter.load(); }   // 0..1 sibilance GR
    float getGateMeter()   const noexcept { return gateMeter.load(); }    // 0..1 gate closed

    sattari::midi::SattariMidiLearn midiLearn { apvts };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "vocal-master" };   // after apvts
    int currentProgram = 0;

    double currentSampleRate = 44100.0;

    using Filter = juce::dsp::IIR::Filter<float>;
    std::array<Filter, 2> hpf, tiltLow, tiltHigh, airShelf;
    juce::dsp::LinkwitzRileyFilter<float> deessSplit;   // sibilance band split
    std::array<sattari::dsp::EnvelopeFollower, 2> deessEnv;
    sattari::dsp::EnvelopeFollower gateEnv, gateGain, levelEnv;

    // AI stem-removal seam (offline; needs an ONNX model — see docs/ml-stem-separation.md).
    sattari::ml::StemSeparator stemSeparator;

    std::atomic<float> inputMeter { 0.0f }, outputMeter { 0.0f }, deEssMeter { 0.0f }, gateMeter { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariVocalMasterAudioProcessor)
};
