#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include <sattari_audio_core/SattariPsolaShifter.h>
#include <sattari_audio_core/SattariFormantShifter.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <atomic>

// =============================================================================
//  Sattari Morph — pitch + formant character in one row of knobs.
// -----------------------------------------------------------------------------
//  The "different person" box (a key-aware Little AlterBoy). Pitch and Formant
//  shift independently (Formant = perceived body/gender), plus Drive and Mix.
//  Mode does the differentiating work:
//    Shift    — straight pitch/formant, no correction.
//    Quantize — snap the pitch to the SESSION-BUS key (which AlterBoy can't do).
//    Robot    — lock output to one note, monotone (the T-Pain effect).
//  Reuses the shared YIN tracker, TD-PSOLA shifter, and cepstral formant warp.
//  The dry path is delayed to match the wet latency so Mix never comb-filters.
// =============================================================================
class SattariMorphAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariMorphAudioProcessor();
    ~SattariMorphAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Morph"; }
    bool acceptsMidi() const override { return false; }
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

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pullBusScale();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank;
    int currentProgram = 0;

    sattari::dsp::PitchDetector  pitch_;
    sattari::dsp::PsolaShifter   psola_;
    sattari::dsp::FormantShifter formant_;

    std::vector<float> dryMono_, wet_, dryDelay_;
    int   dryPos_ = 0, totalLatency_ = 0, hopCount_ = 0;
    double sr_ = 48000.0, lastPeriod_ = 240.0;
    float lastScale_ = 1.0f;

    std::array<bool, 12> busScale_ { };
    juce::int64 lastBusPollMs_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariMorphAudioProcessor)
};
