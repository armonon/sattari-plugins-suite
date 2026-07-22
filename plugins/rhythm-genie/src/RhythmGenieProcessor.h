#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariDrumKit.h>
#include <sattari_audio_core/SattariDrumPads.h>
#include <sattari_audio_core/SattariDrumLooper.h>
#include <sattari_audio_core/SattariBeatGrid.h>
#include <sattari_audio_core/SattariGroove.h>
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

    // --- Editable beat grid (drives all 8 kit lanes) --------------------------
    static constexpr int kGridLanes = 8;   // one per kit voice
    static int  kitLaneNote (int lane) noexcept;       // GM note per lane (8 voices)
    const sattari::dsp::BeatGrid& beatGrid() const noexcept { return grid; }
    void regenerateGrid();                 // fill lanes 0-3 from the current style + density
    void clearGrid() noexcept { grid.clear(); }
    void toggleCell (int lane, int step) noexcept { grid.toggle (lane, step); }

    // --- Per-lane sample slots ------------------------------------------------
    // When a lane has a sample loaded, triggering it plays the sample instead of the synth
    // voice. Loading reads the file on the message thread; the audio thread only copies a
    // reference-counted pointer under a try-lock, so nothing allocates or frees on it.
    bool loadSampleForLane (int lane, const juce::File& file);   // message thread
    void clearSampleForLane (int lane) noexcept;
    bool laneHasSample (int lane) const noexcept;
    juce::String laneSampleName (int lane) const;
    // Install a decoded buffer directly (used by loadSampleForLane and offline tests).
    void setLaneSampleBuffer (int lane, juce::AudioBuffer<float> buf, double sourceSr, const juce::String& name);

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
    void renderKitSegment (float* out, int numSamples) noexcept;   // synth voices + sample voices
    void triggerLane (int lane, float velocity) noexcept;          // sample if loaded, else synth
    sattari::dsp::DrumVoiceParams laneVoiceParams (int lane) const noexcept;

    // Reference-counted sample data, so an in-flight voice keeps its buffer alive even after
    // the slot is swapped/cleared; everything is retained until the processor is destroyed,
    // so a buffer is never freed on the audio thread.
    struct SampleSlot : juce::ReferenceCountedObject
    {
        juce::AudioBuffer<float> data;
        double sourceSr = 44100.0;
        juce::String name;
        using Ptr = juce::ReferenceCountedObjectPtr<SampleSlot>;
    };
    struct SamplePlayback { SampleSlot::Ptr buf; double pos = 0.0, rate = 1.0; float gain = 1.0f; bool active = false; };

    SampleSlot::Ptr  laneSample[kGridLanes];              // current sample per lane (null = synth)
    SamplePlayback   samplePlay[kGridLanes];              // active playback state per lane
    juce::String     laneSamplePath[kGridLanes];          // for state persistence
    juce::ReferenceCountedArray<SampleSlot> retainedSamples;   // keeps every loaded slot alive (msg thread)
    juce::SpinLock   sampleLock;                          // guards the laneSample swap
    juce::AudioFormatManager formatManager;

    sattari::dsp::DrumKit<kKitLanes> kit;
    sattari::dsp::DrumPadMap         padMap { kKitLanes };
    sattari::dsp::DrumLooper         looper;       // live-pad loop ("looper feel")
    sattari::dsp::BeatGrid           grid;         // editable step grid (8 lanes x 16 steps)
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
