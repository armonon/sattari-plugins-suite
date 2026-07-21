#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/modules/PeakModule.h>
#include <array>
#include <atomic>
#include <vector>

// =============================================================================
//  Sattari Peak — true-peak-aware brickwall limiter / maximizer
// -----------------------------------------------------------------------------
//  A lookahead brickwall limiter. The dry signal is delayed through a ring
//  buffer while a peak detector (optionally 4x-oversampled to catch inter-
//  sample / "true" peaks, the same detector Sattari Meter uses) scans the
//  window ahead. Whenever an upcoming peak would exceed the ceiling, the gain
//  envelope drops INSTANTLY (over the lookahead) to the exact reduction needed,
//  then recovers with a program-dependent release. Because the reduction is in
//  place before the loud sample reaches the output, |out| <= ceiling is
//  guaranteed; a final true-peak-aware soft ceiling catches any residual
//  inter-sample overshoot so the output never crosses 0 dBFS.
//
//  A one-knob "Target LUFS" mode measures short-term BS.1770 loudness (K-
//  weighted, same as Sattari Meter) and slowly auto-drives the input push so
//  integrated loudness approaches the target while the ceiling still holds.
//
//  Editor polls live gain-reduction, current true-peak and short-term LUFS via
//  lock-free atomics.
// =============================================================================
class SattariPeakAudioProcessor final : public juce::AudioProcessor,
                                        private juce::AsyncUpdater
{
public:
    SattariPeakAudioProcessor();
    ~SattariPeakAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void reset() override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Peak"; }
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

    using Readout = sattari::dsp::PeakModule::Readout;
    Readout getReadout() const noexcept { return module.getReadout(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    bool  boolParam (const char* id) const noexcept { return param (id) > 0.5f; }



    juce::AudioProcessorValueTreeState apvts;
    double sampleRate = 44100.0;

    // Thin wrapper: the standalone plugin and a rack slot run the SAME module.
    sattari::dsp::PeakModule module;
    // Lookahead latency is published on the message thread — announcing it from
    // processBlock (as this plugin used to) is not realtime-safe.
    void handleAsyncUpdate() override;
    std::atomic<int> pendingLatency { 0 };
    int reportedLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariPeakAudioProcessor)
};
