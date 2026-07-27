#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/modules/MultibandModule.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <array>
#include <atomic>

// =============================================================================
//  Sattari Multiband — 3-band multiband compressor
// -----------------------------------------------------------------------------
//  Two Linkwitz-Riley crossovers split the signal into low / mid / high bands
//  that recombine perfectly flat when untouched (the classic allpass 3-way split
//  mirrored from plugins/clean). Each band runs an independent feed-forward
//  compressor — the exact log-domain, soft-knee gain computer and asymmetric
//  attack/release smoother from plugins/comp (shared EnvelopeFollower's
//  processFalling primitive) — plus its own makeup gain and a solo toggle.
//
//  Signal path per band:
//     stereo-max detector -> log-domain gain computer (threshold/ratio/knee)
//                         -> attack/release smoothing on the GR envelope (dB)
//                         -> band gain + per-band makeup
//  Summed bands -> global makeup is folded into per-band makeup; a global dry/wet
//  MIX blends the summed compressed result against the untouched input, and a
//  transparent soft ceiling bounds the output. Vocal-tuned defaults (crossovers
//  at 250 Hz / 3 kHz, gentle mid ratio).
//
//  Preallocated in prepareToPlay; nothing allocates or locks in processBlock.
//  Per-band gain reduction is published to the editor via lock-free atomics.
//
//  REFERENCE: plugins/clean (LR 3-band crossover) + plugins/comp (per-band
//  compressor gain computer + EnvelopeFollower). Reuses SattariEnvelopeFollower.
// =============================================================================
class SattariMultibandAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kBands = 3;

    SattariMultibandAudioProcessor();
    ~SattariMultibandAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Multiband"; }
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

    // Lock-free readout for the editor: per-band gain reduction magnitude (>= 0 dB).
    float getBandGainReductionDb (int band) const noexcept
    {
        return module.currentBandGrDb (juce::jlimit (0, kBands - 1, band));
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "multiband" };   // after apvts
    int currentProgram = 0;

    // Thin wrapper: the standalone plugin and the Mix rack slot run the SAME module.
    sattari::dsp::MultibandModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariMultibandAudioProcessor)
};
