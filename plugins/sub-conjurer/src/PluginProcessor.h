#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <sattari_audio_core/SattariRealtime.h>

class SubConjurerAudioProcessor final : public juce::AudioProcessor
{
public:
    SubConjurerAudioProcessor();
    ~SubConjurerAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { sessionReader.stop(); }

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

    struct UiState
    {
        float frequencyHz = 55.0f;
        float envelope = 0.0f;
        float limiterGain = 1.0f;
        float inputPeak = 0.0f;
        float outputPeak = 0.0f;
        bool followingKey = false;
    };
    UiState currentUiState() const noexcept { return uiState.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();

    float midiNoteToHz (int note) const;
    int noteNameToMidi (int index) const;

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "sub-conjurer" };   // after apvts
    int currentProgram = 0;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:
    sattari::xplugin::SessionReader sessionReader;   // cross-plugin "Follow Song Key"

    double currentSampleRate = 44100.0;
    float oscillatorPhase = 0.0f;
    sattari::dsp::EnvelopeFollower envelopeFollower;
    float smoothedHz = 55.0f;   // current glided oscillator frequency
    float targetHz = 55.0f;     // block target the oscillator glides toward
    float limiterState = 1.0f;
    std::vector<float> monoScratch;

    // Shared suite pitch follower, tuned to the sub range (25-160 Hz), analysed on
    // a decimated hop schedule (the long analysis window spans several host blocks).
    sattari::dsp::PitchDetector pitchDetector;
    int analysisHop = 512;
    int analysisAccumulator = 0;
    float limiterRecoveryCoeff = 0.001f;
    sattari::rt::AtomicSnapshot<UiState> uiState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubConjurerAudioProcessor)
};
