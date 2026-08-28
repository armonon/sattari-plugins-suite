#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <atomic>
#include <vector>

// =============================================================================
//  Sattari Tuner — precision instrument / vocal tuner
// -----------------------------------------------------------------------------
//  A pass-through analyser: audio goes through untouched while the plugin tracks
//  the incoming pitch and reports the nearest note plus how many cents sharp or
//  flat you are. Uses the shared YIN PitchDetector (the same monophonic tracker
//  Auto Pitch relies on), folded to the octave and smoothed so the readout is
//  steady. Adjustable reference A (415-466 Hz) for orchestral / period tuning.
//
//  Everything is allocation-free after prepareToPlay; the editor polls a
//  lock-free LiveReading snapshot on a timer.
// =============================================================================
class SattariTunerAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariTunerAudioProcessor();
    ~SattariTunerAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Tuner"; }
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

    // Lock-free snapshot for the editor timer. Trivially copyable.
    struct LiveReading
    {
        bool  voiced      = false;
        int   midiNote    = -1;      // nearest note (0..127) at the current reference A
        float cents       = 0.0f;    // signed deviation from that note, -50..+50
        float frequencyHz = 0.0f;
        float confidence  = 0.0f;    // 0..1
    };
    LiveReading getReading() const noexcept
    {
        LiveReading r;
        r.voiced      = liveVoiced.load (std::memory_order_relaxed);
        r.midiNote    = liveMidi.load (std::memory_order_relaxed);
        r.cents       = liveCents.load (std::memory_order_relaxed);
        r.frequencyHz = liveHz.load (std::memory_order_relaxed);
        r.confidence  = liveConf.load (std::memory_order_relaxed);
        return r;
    }

    static juce::String noteName (int midiNote); // e.g. 69 -> "A4"

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }

    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "tuner" };   // after apvts
    int currentProgram = 0;

    double sampleRate = 44100.0;
    sattari::dsp::PitchDetector detector;
    std::vector<float> monoScratch;    // preallocated mono-sum buffer

    // Smoothed pitch (in MIDI cents) for a steady readout; -1 = disarmed.
    double smoothedMidi = -1.0;

    std::atomic<bool>  liveVoiced { false };
    std::atomic<int>   liveMidi   { -1 };
    std::atomic<float> liveCents  { 0.0f };
    std::atomic<float> liveHz     { 0.0f };
    std::atomic<float> liveConf   { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariTunerAudioProcessor)
};
