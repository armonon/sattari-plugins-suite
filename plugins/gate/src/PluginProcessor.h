#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

    // Lock-free readouts for the editor.
    float getGainReductionDb() const noexcept { return gainReductionDb.load (std::memory_order_relaxed); }
    bool  getGateOpen()        const noexcept { return gateOpenPublished.load (std::memory_order_relaxed); }
    bool  getTempoGateActive() const noexcept { return tempoGateActive.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    bool  boolParam (const char* id) const noexcept { return param (id) > 0.5f; }
    int   choiceParam (const char* id) const noexcept { return (int) std::round (param (id)); }

    // Detector sidechain band-pass (one-pole HP then one-pole LP), per channel-summed.
    float sidechainFilter (float input, float hpHz, float lpHz) noexcept;
    double resolveBpm();

    juce::AudioProcessorValueTreeState apvts;
    double sampleRate = 44100.0;

    // Detector envelope (fast) and the gain-envelope smoother own the timing.
    sattari::dsp::EnvelopeFollower detectorFollower;   // rectified peak follower
    float rmsSquare = 0.0f;                             // running mean-square for RMS mode

    // Gain envelope: a linear 0..1 gate gain, ramped by attack/release toward a
    // target (1 = open, floor = closed), with a hold counter keeping it open.
    float currentGateGain = 0.0f;      // 0..1 smoothed gate gain
    int   holdCounter = 0;             // samples remaining before close is allowed
    bool  gateOpen = false;            // hysteresis state

    // Sidechain filter state (HP one-pole + LP one-pole).
    float hpX1 = 0.0f, hpY1 = 0.0f;
    float lpY1 = 0.0f;

    // Lookahead delay line (main path is delayed so the detector can pre-open).
    juce::AudioBuffer<float> lookaheadRing;
    int lookaheadRingPos = 0;
    int lookaheadRingLen = 1;
    int maxLookaheadSamples = 0;
    int reportedLatency = -1;

    // Tempo-gate phase (free-runs off BPM when transport is stopped).
    double syncPhase = 0.0;

    sattari::xplugin::TempoReader tempoReader;   // cross-plugin TEMPO bus consumer

    std::atomic<float> gainReductionDb   { 0.0f };  // published GR magnitude (>= 0)
    std::atomic<bool>  gateOpenPublished { false };
    std::atomic<bool>  tempoGateActive   { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariGateAudioProcessor)
};
