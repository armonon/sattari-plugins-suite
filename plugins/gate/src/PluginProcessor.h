#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/GateModule.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <atomic>

// =============================================================================
//  Sattari Gate — noise gate + downward expander
// -----------------------------------------------------------------------------
//  A full-featured dynamics gate. Signal path:
//
//    detector (peak|RMS on the stereo-max) -> sidechain HP/LP band-pass
//                          -> hysteresis state machine (dual threshold)
//                          -> attack / hold / release gain envelope
//                          -> range floor + expander scaling
//                          -> lookahead ring (opens just before transients)
//
//  The hysteresis state machine is the heart of it: the gate OPENS when the
//  detector rises above the upper threshold and only CLOSES again once it falls
//  below the lower threshold (upper - hysteresis). A hold time keeps it open for
//  a minimum window after the last time the signal was above the close point, so
//  a signal dithering around one threshold cannot chatter open/closed.
//
//  Two gain floors:
//    * Gate mode  — when closed the gain is a fixed floor set by `range` (dB of
//      attenuation), giving the classic hard noise gate.
//    * Expander mode — the attenuation scales with how far BELOW the (upper)
//      threshold the detector sits: near the threshold barely any reduction,
//      deep below it approaches the full `range`, scaled by `ratio`. A gentle,
//      program-dependent downward expander.
//
//  A lookahead ring delays the wet/dry path so the (undelayed) detector can open
//  the gain just before a transient lands, preserving attacks. Latency is
//  reported to the host and only re-notified when the integer sample count
//  changes.
//
//  Optional TEMPO-GATE mode: a rhythmic trance-gate whose open windows are locked
//  to a musical division of the tempo (host PPQ while playing, else the
//  cross-plugin TEMPO bus, else 120 BPM). Duty cycle sets how much of each cycle
//  is open; the same attack/hold/release smoothing shapes each edge.
//
//  Mirrors plugins/side-chain-master (follower + detector HP + lookahead ring)
//  and plugins/comp (overall structure + tempo bus). Reuses SattariEnvelopeFollower.
// =============================================================================
class SattariGateAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariGateAudioProcessor();
    ~SattariGateAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Gate"; }
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

    // Lock-free readouts for the editor.
    float getGainReductionDb() const noexcept { return module.currentGainReductionDb(); }
    bool  getGateOpen()        const noexcept { return module.isGateOpen(); }
    bool  getTempoGateActive() const noexcept { return module.isTempoGateActive(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Detector sidechain band-pass (one-pole HP then one-pole LP), per channel-summed.
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "gate" };   // after apvts
    int currentProgram = 0;

    // Thin wrapper: the standalone plugin and the Mix rack slot run the SAME module.
    // Parameter IDs are unchanged (empty prefix), so sessions and presets survive.
    sattari::dsp::GateModule module;
    int reportedLatency = -1;   // last value published to the host (-1 forces a first publish)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariGateAudioProcessor)
};
