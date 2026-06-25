#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <array>
#include <memory>

// Sattari Bus - a bus/aux channel strip in the spirit of a Logic bus, with far
// more processing on the channel: input trim, HPF/LPF, 3-band tone, a glue
// compressor, drive, stereo width, pan, parallel mix, output + ceiling, and
// mute/bypass, across four character modes.
class SattariBusAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariBusAudioProcessor();
    ~SattariBusAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Bus"; }
    bool acceptsMidi() const override { return true; }    // for MIDI Learn CC input
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }
    float getGainReductionDb() const noexcept { return gainReductionDb.load (std::memory_order_relaxed); }

private:
    // --- tiny DSP building blocks (hand-rolled, no extra JUCE modules) --------
    struct OnePole
    {
        float a = 0.0f, z = 0.0f;
        void reset() noexcept { z = 0.0f; }
        // Low-pass one-pole.
        float lp (float x) noexcept { z += a * (x - z); return z; }
        // High-pass = input minus low-pass.
        float hp (float x) noexcept { return x - lp (x); }
    };

    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0; // transposed direct form II
        void reset() noexcept { z1 = z2 = 0; }
        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void setPeak (double fs, double f, double q, double gainDb) noexcept;
        void setLowShelf (double fs, double f, double gainDb) noexcept;
        void setHighShelf (double fs, double f, double gainDb) noexcept;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float getParam (const char* id) const noexcept;

    void setOnePoleLP (OnePole& p, float cutoffHz) noexcept;

    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:
    int currentProgram = 0;
    double sampleRate = 44100.0;

    // Per-channel filter state (index 0 = L, 1 = R).
    std::array<OnePole, 2> hpf, lpf;
    std::array<Biquad, 2> lowShelf, midBell, highShelf;

    // Stereo-linked glue compressor state.
    sattari::dsp::EnvelopeFollower compFollower;     // rising peak detector
    sattari::dsp::EnvelopeFollower grSmoother;       // falling GR smoother
    float smoothedGrDb = 0.0f;
    std::atomic<float> gainReductionDb { 0.0f };

    // Anti-aliased drive: oversample ONLY the drive nonlinearity (4x). The dry
    // path for the parallel mix is delayed by the oversampler latency so the
    // wet/dry blend stays phase-aligned; the plugin reports that latency.
    std::unique_ptr<juce::dsp::Oversampling<float>> driveOS;
    int osChannels = 0;
    juce::AudioBuffer<float> driveDryStash;  // original input for this block
    juce::AudioBuffer<float> driveDryRing;   // latency-comp delay (length = OS latency)
    int driveDryRingLen = 1, driveDryRingPos = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariBusAudioProcessor)
};
