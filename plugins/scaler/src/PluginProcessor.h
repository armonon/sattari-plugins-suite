#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <array>

// =============================================================================
//  Sattari Scaler — key-following chord & scale MIDI generator
// -----------------------------------------------------------------------------
//  A MIDI effect: play single notes and get in-key chords out. Every input note
//  is snapped into the current scale and (optionally) turned into a diatonic
//  chord built by stacking scale degrees, so one finger plays the right triad or
//  seventh for the key. Follow Song Key reads the master bus (Sattari Key) so it
//  always harmonises to the detected key. Reuses the shared scale tables + bus.
// =============================================================================
class SattariScalerAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariScalerAudioProcessor();
    ~SattariScalerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Scaler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }
    static juce::StringArray rootChoices();
    static juce::StringArray scaleChoices();
    static juce::StringArray chordChoices();

    // Effective key for the editor's KeyButton.
    struct KeyState { bool following = false; int root = 0; int scale = 1; };
    KeyState currentKeyState() const;

    // Exposed for the contract: build the chord for one input note.
    int buildChord (int inputNote, int keyRoot, sattari::dsp::ScaleId id, int chordType,
                    int* out, int maxOut) const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    void resolveKey (int& root, sattari::dsp::ScaleId& id) const;

    juce::AudioProcessorValueTreeState apvts;
    sattari::xplugin::SessionReader sessionReader;

    // For each held input note, the output notes we sent (for matching note-offs).
    struct Held { int count = 0; std::array<int, 5> notes { { -1,-1,-1,-1,-1 } }; int channel = 1; };
    std::array<Held, 128> held;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariScalerAudioProcessor)
};
