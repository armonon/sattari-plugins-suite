#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/modules/MeterModule.h>
#include <array>
#include <atomic>
#include <vector>

// =============================================================================
//  Sattari Meter — loudness, true-peak & correlation
// -----------------------------------------------------------------------------
//  A pass-through analyser (audio is never altered) that reports ITU-R BS.1770
//  loudness — momentary (400 ms), short-term (3 s) and an integrated average —
//  plus 4x-oversampled true-peak and L/R correlation. Uses proper K-weighting
//  (a ~+4 dB high-shelf at 1.68 kHz feeding a 38 Hz high-pass). Editor polls the
//  results on a timer.
// =============================================================================
class SattariMeterAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariMeterAudioProcessor();
    ~SattariMeterAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void reset() override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Meter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
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

    using Readout = sattari::dsp::MeterModule::Readout;
    Readout getReadout() const noexcept { return module.getReadout(); }
    void resetIntegrated() { module.resetIntegrated(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();


    juce::AudioProcessorValueTreeState apvts;
    double sampleRate = 44100.0;

    // Thin wrapper: the standalone plugin and a rack slot run the SAME module.
    sattari::dsp::MeterModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariMeterAudioProcessor)
};
