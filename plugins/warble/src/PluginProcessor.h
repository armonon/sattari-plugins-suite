#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <array>
#include <vector>

// =============================================================================
//  Sattari Warble — tape / vinyl lo-fi character box
// -----------------------------------------------------------------------------
//  A single "Age" macro drives a chain of tape/vinyl degradation:
//    * Wow & flutter  — a modulated fractional delay line (slow wow LFO + a
//      faster flutter LFO) produces pitch drift/wobble.
//    * Tape saturation — a tanh drive stage softens and compresses peaks.
//    * Head-bump / tone — a low-shelf bump plus a gentle high-cut for a warm,
//      band-limited "old-machine" tone.
//    * Hiss           — filtered low-level noise floor.
//    * Crackle        — sparse impulsive vinyl pops (optional).
//  "Age" scales wow depth, flutter depth, saturation drive, hiss and crackle
//  amounts together; each also has its own trim. The plugin keeps its own
//  dry/wet mix so mix=0 (or Age=0 with the mix-scaled effects) is a clean
//  pass-through. Allocation-free after prepareToPlay, RT-safe, bounded output.
// =============================================================================
class SattariWarbleAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariWarbleAudioProcessor();
    ~SattariWarbleAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Warble"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "warble" };   // after apvts
    int currentProgram = 0;
    double sampleRate = 44100.0;

    // --- Modulated fractional delay line (wow + flutter) ---------------------
    std::array<std::vector<float>, 2> delayBuf;
    int   delayLen = 0;
    int   delayWrite = 0;
    float baseDelaySamples = 0.0f;   // centre delay the modulation swings around

    // LFO phases (shared across channels; a small per-channel offset decorrelates).
    double wowPhase = 0.0;
    double flutPhase = 0.0;

    // --- Tone filters (per channel): low-shelf head bump + one-pole high-cut --
    struct LowShelf
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }
        void set (double fs, double f, double gainDb) noexcept;
        float process (float x) noexcept
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };
    std::array<LowShelf, 2> headBump;
    std::array<float, 2>    hiCutState { { 0.0f, 0.0f } };
    float hiCutCoeff = 1.0f;   // one-pole LP coefficient for the tape high-cut

    // --- Hiss + crackle noise generators -------------------------------------
    juce::Random rng { 0x5A77A21 };
    std::array<float, 2> hissLpState { { 0.0f, 0.0f } };   // shape the white hiss
    std::array<float, 2> crackleEnv  { { 0.0f, 0.0f } };   // decaying pop envelope

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariWarbleAudioProcessor)
};
