#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include "RhythmPatterns.h"
#include <atomic>
#include <vector>

// =============================================================================
//  Sattari Rhythm Genie — host-synced MIDI drum-pattern generator
// -----------------------------------------------------------------------------
//  Picks a groove (style) and emits General-MIDI drum notes locked to the host
//  transport (PPQ), with Density / Swing / Velocity / Gate / Humanize controls
//  and per-lane mutes. In a standalone it free-runs at the host BPM so you can
//  audition it. Audio passes through silent; the MIDI feeds a drum sampler.
// =============================================================================
class SattariRhythmGenieAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariRhythmGenieAudioProcessor();
    ~SattariRhythmGenieAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Rhythm Genie"; }
    bool acceptsMidi() const override { return true; }
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

    static juce::StringArray styleChoices();
    int  currentStep() const noexcept { return playStep.load(); }
    bool laneEnabled (int lane) const noexcept;

    sattari::midi::SattariMidiLearn midiLearn { apvts };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    void  emitStep (int stepIdx, int sampleOffset, juce::MidiBuffer& out, double samplesPerStep);
    void  allNotesOff (juce::MidiBuffer& out);

    juce::AudioProcessorValueTreeState apvts;

    double currentSampleRate = 44100.0;
    double internalPpq = 0.0;          // free-run clock when the host has no PPQ
    bool   wasGenerating = false;

    struct PendingOff { int note; int samplesRemaining; };
    std::vector<PendingOff> pendingOffs;
    juce::Random rng;
    std::atomic<int> playStep { -1 };  // current 16th-step for the editor grid

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariRhythmGenieAudioProcessor)
};
