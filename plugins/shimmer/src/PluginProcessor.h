#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include <sattari_audio_core/SattariPsolaShifter.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <array>
#include <atomic>
#include <vector>

// =============================================================================
//  Sattari Shimmer — pitch-shifted "shimmer" reverb
// -----------------------------------------------------------------------------
//  A reverb tank whose feedback path contains a pitch shifter, so the tail
//  blooms upward into a crystalline pad. Signal flow per block:
//
//    dry --> [pre-delay] --> tank input += shimmer * shifted(prev wet)
//                              |
//                           juce::Reverb  (taken as PURE wet, dryLevel 0)
//                              |
//                            wet --> [tone/damp one-pole LP] --> PSOLA(+interval)
//                                        (feeds back into next block's input)
//                              |
//              out = dry*(1-mix) + wet*mix     (our OWN mix, mix=0 = pass-through)
//
//  The shimmer amount scales the fed-back shifted signal; because it re-enters a
//  decaying tank at amount < 1 and is low-passed each pass, the loop is bounded
//  (energy shed by the reverb decay + damping exceeds the shimmer re-injection).
//
//  Key Snap (optional): reads the Sattari KEY bus (SessionReader) via the shared
//  KeyButton, and quantizes the +12/+7/+5 shimmer interval so the transposed
//  pitch class lands on an in-key scale tone — the bloom never clashes with the
//  song. Allocation-free after prepareToPlay; UI reads lock-free atomics only.
// =============================================================================
class SattariShimmerAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariShimmerAudioProcessor();
    ~SattariShimmerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Shimmer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

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
    static juce::StringArray intervalChoices();

    // Effective key state for the editor's KeyButton (whether Key Snap is on and
    // the key it is following). rootChoices / scaleChoices mirror the KEY bus.
    static juce::StringArray rootChoices();
    static juce::StringArray scaleChoices();
    struct KeyState { bool snapping = false; int root = 0; int scale = 1; };
    KeyState currentKeyState() const;

    // Exposed for the contract: the effective (possibly key-snapped) shimmer
    // interval in semitones for a given base interval + key.
    int effectiveShimmerSemis (int baseSemis, int keyRoot, sattari::dsp::ScaleId id) const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    static int baseSemisFor (int intervalIndex) noexcept;   // 0->+12, 1->+7, 2->+5
    void resolveKey (int& root, sattari::dsp::ScaleId& id) const;
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "shimmer" };   // after apvts
    int currentProgram = 0;
    sattari::xplugin::SessionReader sessionReader;   // consumes the KEY bus
    double sampleRate = 44100.0;

    // Reverb tank, taken as pure wet (Space pattern; own dry/wet mix).
    juce::Reverb reverb;
    juce::AudioBuffer<float> dryScratch;   // dry(+pre-delay) held for our mix
    juce::AudioBuffer<float> tankIn;       // pre-delayed input + shimmer injection
    juce::AudioBuffer<float> shiftScratch; // shifted wet tail before re-injection

    // Pre-delay line (per channel).
    std::array<std::vector<float>, 2> preBuf;
    int preLen = 0, preWrite = 0;

    // Pitch shifter in the feedback path (formant-preserving, per channel).
    std::vector<sattari::dsp::PsolaShifter> psola;

    // Damping (tone) one-pole LP state on the shimmer feedback, per channel.
    std::array<float, 2> toneState { { 0.0f, 0.0f } };

    // Held shifted tail from the previous block, re-injected into this block's
    // tank input (closes the feedback loop without allocating).
    juce::AudioBuffer<float> fbCarry;

    // Live readout for the UI.
    std::atomic<int> liveInterval { 0 };   // effective interval in semitones

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariShimmerAudioProcessor)
};
