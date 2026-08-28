#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/modules/CompModule.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <atomic>

// =============================================================================
//  Sattari Comp — vocal-voiced feed-forward compressor
// -----------------------------------------------------------------------------
//  A full-featured, standalone dynamics box tuned for vocals. Signal path:
//
//    detector (stereo-max) -> sidechain high-pass one-pole  (de-emphasise lows)
//                          -> log-domain gain computer (threshold/ratio/knee)
//                          -> asymmetric attack/release smoother (EnvelopeFollower)
//                          -> gain + makeup, dry/wet (parallel) blend
//
//  The gain computer works in dB with a soft-knee interpolation, exactly the
//  classic feed-forward topology. The attack/release smoothing runs on the
//  gain-reduction envelope (in dB) using the shared suite EnvelopeFollower's
//  processFalling() primitive (fast toward MORE reduction, slow on recovery).
//
//  Extras: auto-release (program-dependent release), auto make-up gain, a
//  sidechain-listen solo toggle, and a "tempo release" mode where the release
//  time is a musical note division read off the cross-plugin TEMPO bus
//  (TempoReader) with a host-BPM (then 120) fallback. Live gain reduction is
//  published to the editor via a lock-free atomic.
//
//  Mirrors plugins/side-chain-master (detector + follower + GR meter) and
//  plugins/transient (overall plugin structure). Reuses SattariEnvelopeFollower.
// =============================================================================
class SattariCompAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariCompAudioProcessor();
    ~SattariCompAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Comp"; }
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

    // Lock-free readouts for the editor — now answered by the module, which owns the
    // DSP and therefore the meters.
    float getGainReductionDb() const noexcept { return module.currentGainReductionDb(); }
    double getEffectiveReleaseMs() const noexcept { return module.currentEffectiveReleaseMs(); }
    bool  getTempoReleaseActive() const noexcept { return module.isTempoReleaseActive(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "comp" };   // after apvts
    int currentProgram = 0;

    // Thin wrapper: the standalone plugin and the Mix rack slot run the SAME module.
    // Parameter IDs are unchanged (the standalone links with an empty prefix), so
    // existing sessions and presets keep working.
    sattari::dsp::CompModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariCompAudioProcessor)
};
