#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariDrumKit.h>
#include <sattari_audio_core/SattariDrumPads.h>
#include <sattari_audio_core/SattariDrumLooper.h>
#include "RhythmPatterns.h"
#include <array>
#include <atomic>
#include <vector>

// =============================================================================
//  Sattari Rhythm Genie — host-synced MIDI drum-pattern generator
// -----------------------------------------------------------------------------
//  Picks a groove (style) and emits General-MIDI drum notes locked to the host
//  transport (PPQ), with Density / Swing / Velocity / Gate / Humanize controls
//  and per-lane mutes. In a standalone it free-runs at the host BPM so you can
//  audition it. Audio passes through silent; the MIDI feeds a drum sampler.
// =============================================================================
class SattariRhythmGenieAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariRhythmGenieAudioProcessor();
    ~SattariRhythmGenieAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Rhythm Genie"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }   // longest drum tail (ride/808)

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

    static juce::StringArray styleChoices();
    int  currentStep() const noexcept { return playStep.load(); }
    bool laneEnabled (int lane) const noexcept;

    // --- Editor -> audio live triggering (all message-thread, lock-free) --------
    // The editor pushes gestures here; the audio thread drains them and does all pad-map
    // binding, so learn ("Set" then play a note/key) resolves on the audio thread.
    void pushLiveNote (int note, float velocity) noexcept;   // on-screen keys / external note
    void pushLiveKey  (int keyCode, float velocity) noexcept; // computer-keyboard pad
    void pushLiveLane (int lane, float velocity) noexcept;    // direct pad-button hit
    void armLearn (int pad) noexcept { padMap.armLearn (pad); }
    void cancelLearn() noexcept { padMap.cancelLearn(); }
    int  learningPad() const noexcept { return padMap.learning(); }
    int  padMidiNote (int pad) const noexcept { return padMap.midiNote (pad); }
    int  padKeyCode  (int pad) const noexcept { return padMap.keyCode (pad); }

    // Publish the current tempo + transient-pulse count to the cross-plugin tempo
    // bus. Called from the editor timer (message thread — file I/O is fine there),
    // throttled internally, so Rhythm Genie acts as the suite's tempo source.
    void publishTempoToBus();

    sattari::midi::SattariMidiLearn midiLearn { apvts };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    void  emitStep (int stepIdx, int sampleOffset, juce::MidiBuffer& out, double samplesPerStep);
    void  allNotesOff (juce::MidiBuffer& out);

    // --- Audio drum engine ---------------------------------------------------
    // Rhythm Genie is now an instrument: the same step decisions that emit MIDI also
    // trigger a synth kit, and incoming MIDI notes / the editor's pads play it live.
    static constexpr int kKitLanes = 8;   // 0 kick 1 snare 2 chat 3 ohat 4 clap 5 tom 6 rim 7 ride
    void queueKitHit (int lane, float velocity, int sampleOffset) noexcept;
    void renderKit (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;
    sattari::dsp::DrumVoiceParams laneVoiceParams (int lane) const noexcept;

    sattari::dsp::DrumKit<kKitLanes> kit;
    sattari::dsp::DrumPadMap         padMap { kKitLanes };
    sattari::dsp::DrumLooper         looper;       // live-pad loop ("looper feel")
    double loopClockBeats = 0.0;                   // free-running loop position when stopped
    bool   wasLoopOn = false;                      // detect enable edge -> fresh capture
    std::vector<float>               kitScratch;   // mono render buffer (sized in prepare)
    struct KitHit { int lane; float velocity; int offset; };
    static constexpr int kMaxKitHits = 256;
    std::array<KitHit, kMaxKitHits>  kitHits {};
    int kitHitCount = 0;

    // Shaping-param pointers cached in prepare so the audio thread never touches the
    // APVTS map or builds parameter-id strings while rendering.
    std::atomic<float>* laneLevelP[sattari::rhythm::kNumLanes] {};
    std::atomic<float>* laneTuneP [sattari::rhythm::kNumLanes] {};
    std::atomic<float>* laneDecayP[sattari::rhythm::kNumLanes] {};
    std::atomic<float>* kitLevelP = nullptr;

    // Lock-free SPSC FIFO: editor (message thread) -> audio thread live hits.
    struct LiveHit { int kind; int id; float velocity; };   // kind 0 note, 1 key, 2 lane
    static constexpr int kLiveFifoSize = 64;
    std::array<LiveHit, kLiveFifoSize> liveFifo {};
    std::atomic<int> liveWrite { 0 };
    std::atomic<int> liveRead  { 0 };
    void pushLive (int kind, int id, float velocity) noexcept;
    void drainLiveHits (bool recordToLoop, double phase) noexcept;

    juce::AudioProcessorValueTreeState apvts;

    double currentSampleRate = 44100.0;
    double internalPpq = 0.0;          // free-run clock when the host has no PPQ
    bool   wasGenerating = false;

    struct PendingOff { int note; int samplesRemaining; };
    static constexpr int kMaxPendingOffs = 256;
    std::array<PendingOff, kMaxPendingOffs> pendingOffs {};
    std::array<PendingOff, kMaxPendingOffs> pendingOffScratch {};
    int pendingOffCount = 0;
    int pendingOffScratchCount = 0;
    juce::Random rng;
    std::atomic<int> playStep { -1 };  // current 16th-step for the editor grid

    // Tempo-bus publishing (audio thread writes atomics; editor timer publishes).
    std::atomic<double>      pubBpm { 120.0 };
    std::atomic<juce::int64> pubPulse { 0 };
    juce::int64 lastPublishMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariRhythmGenieAudioProcessor)
};
