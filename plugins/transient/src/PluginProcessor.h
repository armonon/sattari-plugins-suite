#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <sattari_audio_core/modules/TransientModule.h>
#include <atomic>

// =============================================================================
//  Sattari Transient — attack / sustain designer
// -----------------------------------------------------------------------------
//  Reshapes the envelope of a sound independently of its level. Attack boosts or
//  softens the onset (punch, click); Sustain lengthens or tightens the tail
//  (body, room). Uses the classic differential-envelope method: a fast- and a
//  slow-attack follower difference gives the ATTACK envelope; a fast- and a
//  slow-release follower difference gives the SUSTAIN envelope. Detection is
//  stereo-linked so the image stays put. Reuses the suite EnvelopeFollower.
// =============================================================================
class SattariTransientAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariTransientAudioProcessor();
    ~SattariTransientAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void reset() override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Transient"; }
    bool acceptsMidi() const override { return false; }
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
    float getGainDb() const noexcept { return module.getGainDb(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "transient" };   // after apvts
    int currentProgram = 0;
    double sampleRate = 44100.0;

    // The DSP itself. This shell is a thin wrapper so the standalone plugin and the
    // Mix rack slot run the SAME implementation and can never drift apart.
    sattari::dsp::TransientModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariTransientAudioProcessor)
};
