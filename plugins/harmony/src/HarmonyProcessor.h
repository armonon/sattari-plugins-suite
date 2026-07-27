#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <sattari_audio_core/SattariHarmonyVoicing.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

#if SATTARI_HARMONY_HAS_RUBBERBAND
 #include <rubberband/RubberBandStretcher.h>
#endif

// =============================================================================
//  Sattari Harmony — realtime diatonic vocal harmoniser / choir
// -----------------------------------------------------------------------------
//  Detects the lead vocal pitch (shared YIN tracker), then renders up to four
//  scale-locked harmony voices with one formant-preserving Rubber Band shifter
//  each. The key is set manually OR followed live from the cross-plugin session
//  bus (the key Auto Pitch publishes) — the suite's signature trick: harmonies
//  that already know the song's key. Per voice: interval, level, pan, formant
//  character. Global: blend, stereo spread, humanise, output.
// =============================================================================
class SattariHarmonyAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kNumVoices = 8;   // Pro: 8-voice diatonic stack (was 4)

    SattariHarmonyAudioProcessor();
    ~SattariHarmonyAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Harmony"; }
    bool acceptsMidi() const override { return true; }    // MIDI Learn
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

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

    float getInputMeter()  const noexcept { return inputMeter.load(); }
    float getOutputMeter() const noexcept { return outputMeter.load(); }

    // --- Shared choice lists (single source of truth for layout + editor) -----
    static juce::StringArray rootChoices();
    static juce::StringArray scaleChoices();
    static juce::StringArray intervalChoices();
    static juce::StringArray sourceChoices();   // Scale vs Chord (MIDI)
    static int  intervalDegrees (int choiceIndex) noexcept;  // choice -> scale-degree shift

    // Live readout for the editor (timer-polled, lock-free).
    struct Snapshot
    {
        bool voiced = false;
        int  leadMidi = -1;
        int  root = 0;
        int  scaleChoice = 0;
        bool followingBus = false;
        std::array<int, kNumVoices> voiceMidi { { -1, -1, -1, -1 } };
        std::array<bool, kNumVoices> voiceOn  { { false, false, false, false } };
    };
    Snapshot getSnapshot() const noexcept;

    sattari::midi::SattariMidiLearn midiLearn { apvts };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void resolveKey (int& root, sattari::dsp::ScaleId& scale, int& scaleChoiceOut, bool& fromBus) const;
    void registerFactoryPresets();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "harmony" };   // after apvts
    int currentProgram = 0;
    sattari::xplugin::SessionReader sessionReader;
    sattari::dsp::PitchDetector pitchDetector;

    double currentSampleRate = 44100.0;
    int    preparedBlockSize = 512;

    static constexpr int kSafetyBlockSize = 16384;
    static constexpr int kMaxHeldChordNotes = 32;
    std::vector<float> monoScratch;
    std::array<int, kMaxHeldChordNotes> heldNotes {};      // MIDI notes currently held (the chord, for Chord mode)
    std::array<int, kMaxHeldChordNotes> chordPcs {};       // their pitch classes, rebuilt per block
    int heldNoteCount = 0;
    int chordPcCount = 0;
    int   analysisHop = 0, analysisCounter = 0;
    int   leadMidiLive = -1;
    bool  voicedLive = false;
    float leadHzSmoothed = 0.0f;
    double voicedGain = 0.0;

    std::array<double, kNumVoices> smoothedRatio   { { 1.0, 1.0, 1.0, 1.0 } };
    std::array<double, kNumVoices> humanizeCents   { { 0.0, 0.0, 0.0, 0.0 } };
    std::array<double, kNumVoices> humanizeTarget  { { 0.0, 0.0, 0.0, 0.0 } };
    juce::Random humanizeRng;

    // Choir doubler: a modulated (chorus) Haas copy of each voice, panned opposite.
    std::array<std::vector<float>, kNumVoices> choirRing;
    std::array<int, kNumVoices>    choirWrite { { 0, 0, 0, 0 } };
    std::array<double, kNumVoices> choirPhase { { 0.0, 0.0, 0.0, 0.0 } };

    juce::AudioBuffer<float> voiceBuffer;    // mono per-voice render scratch
    juce::AudioBuffer<float> harmonyAccum;   // stereo sum of all voices
    juce::dsp::DelayLine<float> dryDelay { 1 << 16 };  // aligns dry to the shifters' latency
    int reportedLatency = 0;

    std::atomic<float> inputMeter { 0.0f }, outputMeter { 0.0f };

    // Live-snapshot atomics (written on audio thread, read by editor timer).
    std::atomic<int>  snapLeadMidi { -1 };
    std::atomic<bool> snapVoiced { false };
    std::atomic<int>  snapRoot { 0 }, snapScale { 0 };
    std::atomic<bool> snapBus { false };
    std::array<std::atomic<int>,  kNumVoices> snapVoiceMidi;
    std::array<std::atomic<bool>, kNumVoices> snapVoiceOn;

#if SATTARI_HARMONY_HAS_RUBBERBAND
    std::array<std::unique_ptr<RubberBand::RubberBandStretcher>, kNumVoices> stretchers;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariHarmonyAudioProcessor)
};
