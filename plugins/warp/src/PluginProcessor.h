#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <array>
#include <vector>

// =============================================================================
//  Sattari Warp — creative vocal transformer
// -----------------------------------------------------------------------------
//  A destructive-in-a-good-way vocal FX unit. One big DOSE macro escalates a
//  chain of real DSP blocks from "subtle character" to "fully mangled":
//
//    1. VOWEL   — morphable formant filter (two bandpass resonators sweeping
//                 A-E-I-O-U). Talkbox / vocal-tract warp.
//    2. ROBOT   — ring modulation against a tunable carrier. Metallic / robotic.
//    3. GRIT    — waveshaper drive + bit / sample-rate crush. Dirt and lo-fi.
//    4. STUTTER — beat-repeat: captures a slice and loops it (tempo-synced when
//                 the host gives a tempo). Glitch / stutter textures.
//
//  DOSE scales every block's intensity together so one knob takes you deeper.
//  Everything is allocation-free after prepareToPlay. Pure JUCE + std DSP; the
//  block math is unit-tested in tests/warp_contract.cpp.
// =============================================================================
class SattariWarpAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariWarpAudioProcessor();
    ~SattariWarpAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Warp"; }
    bool acceptsMidi() const override { return true; }   // for MIDI Learn
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

    // Live output level (for the editor's reactive visual). 0..~1.
    float getOutputLevel() const noexcept { return outLevel.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }

    // --- One RBJ bandpass biquad (a formant resonator) ----------------------
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        void setBandpass (double sr, double freq, double q) noexcept;
        inline float process (float x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
        void reset() noexcept { z1 = z2 = 0.0; }
    };

    juce::AudioProcessorValueTreeState apvts;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:
    double sr = 44100.0;
    std::atomic<float> outLevel { 0.0f };
    juce::AudioBuffer<float> dryScratch;      // pre-FX copy for the dry/wet blend

    // Per-channel state (stereo).
    std::array<Biquad, 2> vowelF1, vowelF2;   // two resonators per channel
    std::array<double, 2> ringPhase { { 0.0, 0.0 } };
    std::array<float,  2> crushHold  { { 0.0f, 0.0f } };
    std::array<int,    2> crushCount { { 0, 0 } };

    // Stutter (beat-repeat) buffers, one per channel.
    std::array<std::vector<float>, 2> stutterBuf;
    int stutterCap = 0;         // max slice length in samples (allocated)
    int stutterWrite = 0;       // write cursor into the current capture
    int stutterPlay = 0;        // playback cursor within the captured slice
    int stutterLen = 0;         // current slice length
    int stutterPos = 0;         // samples since last slice boundary

    double hostBpm = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariWarpAudioProcessor)
};
