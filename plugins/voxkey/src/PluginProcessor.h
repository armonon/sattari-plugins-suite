#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include "PitchDetector.h"
#include "VoiceMidiMapper.h"
#include <atomic>
#include <array>

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

    // --- Paraphonic (HPS) detector (alpha) -----------------------------------
    static constexpr int paraFftOrder = 12;            // 4096
    static constexpr int paraFftSize  = 1 << paraFftOrder;
    std::unique_ptr<juce::dsp::FFT> paraFft;
    std::vector<float> paraRing, paraFftBuf, paraHann;
    int paraWritePos = 0, paraSamplesSinceFft = 0;
    std::array<int, 3>  paraCurrentNotes { -1, -1, -1 };
    std::array<int, 3>  paraDetectFrame (double sr);  // returns up to 3 MIDI notes (-1 = none)
    void               paraProcessBlock (const float* mono, int n, double sr, juce::MidiBuffer& midi);
    int                paraFrameCounter = 0;
    std::array<int, 3>  paraActiveNotes { -1, -1, -1 };  // currently-sounding outputs

    std::atomic<float> displayRms { 0.0f };
    std::atomic<float> displayConfidence { 0.0f };
    std::atomic<int> displayDetectedNote { -1 };
    std::atomic<int> displayOutputNote { -1 };
    std::atomic<bool> displayVoiced { false };
    std::atomic<bool> displayMidiActive { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariVoxKeyAudioProcessor)
};
