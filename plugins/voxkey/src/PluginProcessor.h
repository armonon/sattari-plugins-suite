#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include "PitchDetector.h"
#include "VoiceMidiMapper.h"
#include <atomic>

class SattariVoxKeyAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariVoxKeyAudioProcessor();
    ~SattariVoxKeyAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }   // for MIDI Learn
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }
    VoxKeyVoiceMidiMapper::State getDisplayState() const;
    void panic();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    VoxKeyVoiceMidiMapper::Settings readSettings() const;
    void publishDisplayState (const VoxKeyVoiceMidiMapper::State& state);

    juce::AudioProcessorValueTreeState apvts;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:
    VoxKeyPitchDetector pitchDetector;
    VoxKeyVoiceMidiMapper midiMapper;
    juce::AudioBuffer<float> monoScratch;
    std::atomic<bool> panicRequested { false };

    std::atomic<float> displayRms { 0.0f };
    std::atomic<float> displayConfidence { 0.0f };
    std::atomic<int> displayDetectedNote { -1 };
    std::atomic<int> displayOutputNote { -1 };
    std::atomic<bool> displayVoiced { false };
    std::atomic<bool> displayMidiActive { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariVoxKeyAudioProcessor)
};
