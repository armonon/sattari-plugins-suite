#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include <sattari_audio_core/SattariPsolaShifter.h>
#include <array>
#include <vector>

// =============================================================================
//  Sattari Double — natural vocal doubler / thickener
// -----------------------------------------------------------------------------
//  Generates two decorrelated "doubles" of the input and pans them left/right
//  around the dry centre, the way a singer tracking a part twice would widen and
//  thicken. Each double is (1) pitch-detuned a few cents with the shared
//  formant-preserving PSOLA shifter so it never sounds like a chorus whistle,
//  (2) time-offset a few ms, and (3) slowly drifted so the two takes breathe
//  independently. Pure reuse of the suite's PSOLA + YIN engines; allocation-free
//  after prepareToPlay.
// =============================================================================
class SattariDoubleAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariDoubleAudioProcessor();
    ~SattariDoubleAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Double"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }

    juce::AudioProcessorValueTreeState apvts;
    double sampleRate = 44100.0;

    sattari::dsp::PitchDetector detector;

    // Two double voices. Each owns a PSOLA shifter, a modulated delay line, and a
    // slow random-walk drift generator (decorrelated per voice).
    struct Voice
    {
        sattari::dsp::PsolaShifter psola;
        std::vector<float> delay;    // ring buffer
        int   writePos = 0;
        double driftPos = 0.0;       // current drift offset in samples (smoothed)
        double driftTarget = 0.0;
        int    driftCountdown = 0;
        juce::Random rng;
    };
    std::array<Voice, 2> voices;
    std::vector<float> monoScratch, outLScratch, outRScratch, shiftScratch;
    int delayLen = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariDoubleAudioProcessor)
};
