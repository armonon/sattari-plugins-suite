#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <array>
#include <atomic>
#include <memory>

// =============================================================================
//  Sattari Heat — saturation / tone box
// -----------------------------------------------------------------------------
//  A standalone character saturator with four voicings (Tube / Tape /
//  Transformer / Transistor). Each is a distinct waveshaper run inside a 4x
//  oversampler so the harmonics it generates fold far less aliasing back into
//  the audible band; the reported latency is the oversampler's, and the dry
//  parallel-mix path is delay-compensated to match so wet/dry stay phase
//  aligned. A tone-tilt shelf pair sits pre-shaper (dark <-> bright), a
//  bias/asymmetry control skews the curve to add even harmonics (DC-blocked
//  after), and a soft ceiling guarantees the output can never blow up.
//
//  Mirrors Sattari Bus for the oversampled + latency-comped drive plumbing and
//  Royal Chain's ColorModule for the shaper mindset (drive compensation, mix).
// =============================================================================
class SattariHeatAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariHeatAudioProcessor();
    ~SattariHeatAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Heat"; }
    bool acceptsMidi() const override { return true; }   // for MIDI Learn CC input
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
    // Post-shaper drive read-out (0..1) for a UI heat meter.
    float getDriveMeter() const noexcept { return driveMeter.load (std::memory_order_relaxed); }

private:
    // --- tiny building blocks (hand-rolled, no extra JUCE modules) -----------
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
        void setLowShelf (double fs, double f, double gainDb) noexcept;
        void setHighShelf (double fs, double f, double gainDb) noexcept;
    };

    // First-order DC blocker (removes the offset the bias asymmetry injects).
    struct DcBlocker
    {
        float x1 = 0.0f, y1 = 0.0f, r = 0.9975f;
        void reset() noexcept { x1 = y1 = 0.0f; }
        float process (float x) noexcept
        {
            const float y = x - x1 + r * y1;
            x1 = x; y1 = y;
            return y;
        }
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float getParam (const char* id) const noexcept;
    void registerFactoryPresets();

    // The per-model nonlinearity. `pre` is the driven input, `bias` the
    // asymmetry amount (-1..1). Returns a bounded shaped sample.
    static float shape (int model, float pre, float bias) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:
    int currentProgram = 0;
    double sampleRate = 44100.0;

    // Tone-tilt shelf pair (dark<->bright), pre-shaper, per channel.
    std::array<Biquad, 2> lowShelf, highShelf;
    std::array<DcBlocker, 2> dcBlock;

    // Anti-aliased drive: oversample ONLY the shaper (4x). The dry path for the
    // parallel mix is delayed by the oversampler latency so wet/dry stay phase
    // aligned; the plugin reports that latency to the host.
    std::unique_ptr<juce::dsp::Oversampling<float>> driveOS;
    int osChannels = 0;
    juce::AudioBuffer<float> dryStash;   // original input for this block
    juce::AudioBuffer<float> dryRing;    // latency-comp delay (length = OS latency)
    int dryRingLen = 1, dryRingPos = 0;

    std::atomic<float> driveMeter { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariHeatAudioProcessor)
};
