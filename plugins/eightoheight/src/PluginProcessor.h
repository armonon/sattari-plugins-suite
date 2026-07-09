#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariPitchDetector.h>

// =============================================================================
//  Sattari 808 — audio-follow 808 / sub-bass designer (an EFFECT with audio in).
// -----------------------------------------------------------------------------
//  Tracks the fundamental of an incoming bass/vocal with the shared YIN pitch
//  detector, snaps that pitch to the KEY-bus scale (when engaged), and drives a
//  tuned sine/triangle 808 body oscillator under it. The body has portamento
//  glide, a retriggering amp-decay envelope, a drive/click transient, and a
//  sub-harmonic fold, blended against the dry input. Supersets Sub Conjurer:
//  same audio-follow oscillator + envelope spine, but with an explicit 808 body
//  (attack click + amp decay + one-octave-down sub fold) and key-snapped tuning.
// =============================================================================

class Sattari808AudioProcessor final : public juce::AudioProcessor
{
public:
    Sattari808AudioProcessor();
    ~Sattari808AudioProcessor() override = default;

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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

    // Lock-free readout for the editor: the current 808 body frequency in Hz.
    float currentBodyHz() const noexcept { return displayHz.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:
    sattari::xplugin::SessionReader sessionReader;   // cross-plugin "Tune to Key"

    double currentSampleRate = 44100.0;

    // Shared low-frequency pitch tracker, decimated on a hop counter.
    sattari::dsp::PitchDetector pitchDetector;
    int analysisHop = 512;
    int analysisAccumulator = 0;

    // 808 body oscillator + amp-decay envelope state.
    float oscillatorPhase = 0.0f;
    float smoothedHz = 55.0f;    // glided oscillator frequency (per-sample)
    float targetHz = 55.0f;      // block target the oscillator glides toward
    float ampEnv = 0.0f;         // retriggering amp-decay envelope [0, 1]
    float clickEnv = 0.0f;       // short attack click transient [0, 1]

    // Gate/retrigger tracking on the input level.
    float inputEnvFast = 0.0f;   // fast input follower for gating
    bool  noteActive = false;    // true while the input holds a note

    float limiterState = 1.0f;
    float limiterRecoveryCoeff = 0.001f;

    std::vector<float> monoScratch;

    std::atomic<float> displayHz { 55.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sattari808AudioProcessor)
};
