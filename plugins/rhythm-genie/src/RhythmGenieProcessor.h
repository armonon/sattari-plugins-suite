#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/RhythmGenieModule.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <array>

// =============================================================================
//  Sattari Rhythm Genie — host-synced MIDI drum-pattern generator
// -----------------------------------------------------------------------------
//  Picks a groove (style) and emits General-MIDI drum notes locked to the host
//  transport (PPQ), with Density / Swing / Velocity / Gate / Humanize controls
//  and per-lane mutes. In a standalone it free-runs at the host BPM so you can
//  audition it. It is also an audio instrument: the same step decisions drive a
//  synth kit and per-lane sample slots.
//
//  The engine lives in sattari::dsp::RhythmGenieModule, shared with a Sattari
//  Stack layer. This shell keeps what a module cannot have: file loading, the
//  AudioFormatManager, MIDI Learn, and the MIDI OUTPUT (a Stack layer's MIDI has
//  nowhere to go, so inside a stack the sequencer drives its own kit only).
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

    int getNumPrograms() override { return juce::jmax (1, presetBank.numFactoryPresets()); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int i) override
    {
        if (i >= 0 && i < presetBank.numFactoryPresets())
        {
            currentProgram = i;
            presetBank.loadFactoryPreset (i);
            regenerateGrid();   // the pattern lives in the grid — rebuild it from the preset's style/density
        }
    }
    const juce::String getProgramName (int i) override { return presetBank.factoryPresetNames()[i]; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

    static juce::StringArray styleChoices();
    int  currentStep() const noexcept { return module.currentStep(); }
    bool laneEnabled (int lane) const noexcept;

    // --- Editable beat grid (drives all 8 kit lanes) --------------------------
    static constexpr int kGridLanes = sattari::dsp::RhythmGenieModule::kGridLanes;
    static int  kitLaneNote (int lane) noexcept;       // GM note per lane (8 voices)
    const sattari::dsp::BeatGrid& beatGrid() const noexcept { return module.beatGrid(); }
    void regenerateGrid();                 // fill lanes 0-3 from the current style + density
    void clearGrid() noexcept { module.clearGrid(); }
    void toggleCell (int lane, int step) noexcept { module.toggleCell (lane, step); }

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
    void armLearn (int pad) noexcept { module.armLearn (pad); }
    void cancelLearn() noexcept { module.cancelLearn(); }
    int  learningPad() const noexcept { return module.learningPad(); }
    int  padMidiNote (int pad) const noexcept { return module.padMidiNote (pad); }
    int  padKeyCode  (int pad) const noexcept { return module.padKeyCode (pad); }

    // Publish the current tempo + transient-pulse count to the cross-plugin tempo
    // bus. Called from the editor timer (message thread — file I/O is fine there),
    // throttled internally, so Rhythm Genie acts as the suite's tempo source.
    void publishTempoToBus();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "rhythm-genie" };   // after apvts
    int currentProgram = 0;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:

    // Thin wrapper: the standalone plugin and a Stack layer run the SAME engine.
    // Parameter IDs are unchanged (empty prefix), so sessions survive.
    sattari::dsp::RhythmGenieModule module;

    // Message-thread only: decoding files and keeping every loaded slot alive. The
    // module holds the audio-thread reference; it must never free the last one.
    juce::AudioFormatManager formatManager;
    juce::ReferenceCountedArray<sattari::dsp::RhythmGenieModule::SampleSlot> retainedSamples;
    juce::String laneSamplePath[kGridLanes];

    juce::int64 lastPublishMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariRhythmGenieAudioProcessor)
};
