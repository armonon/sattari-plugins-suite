#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <vector>

class SattariArpAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariArpAudioProcessor();
    ~SattariArpAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { sessionReader.stop(); }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Arp"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

private:
    struct HeldNote
    {
        int note = 60;
        int velocity = 100;
        uint64_t order = 0;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void renderArp (juce::MidiBuffer& output, int startSample, int numSamples);
    void handleInputMidi (const juce::MidiMessage& message, int samplePosition, juce::MidiBuffer& output);
    void noteOnInput (int note, int velocity);
    void noteOffInput (int note);
    void clearHeldNotes();
    void stopOutputNotes (juce::MidiBuffer& output, int samplePosition);
    void triggerStep (juce::MidiBuffer& output, int samplePosition);

    void buildStepNotes (std::vector<int>& out) const;
    void buildSortedBaseNotes (std::vector<int>& out) const;
    const std::vector<int>& chooseNotesForStep();
    int chooseVelocity (int sourceVelocity);
    double getStepQuarterNotes() const;
    int getNextStepSamples();
    void refreshHostTempo();
    void handleTransport (juce::MidiBuffer& output);  // panic/reset on stop, restart, loop-wrap
    int snapNoteToScale (int note) const;             // scale-quantize output notes
    void resetSequence();
    bool hasHeldNotes() const { return ! heldNotes.empty(); }

public:
    // Effective key for the editor's KeyButton: the scale + root actually in use
    // (root comes from the bus when Follow Song Key is on and a session is live).
    struct KeyState { bool following = false; int root = 0; int scale = 0; };
    KeyState currentKeyState() const;
private:

    juce::AudioProcessorValueTreeState apvts;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:
    sattari::xplugin::SessionReader sessionReader;   // cross-plugin "Follow Song Key"

    double currentSampleRate = 44100.0;
    double currentBpm = 120.0;
    int samplesUntilNextStep = 0;
    int samplesUntilGateOff = -1;
    int stepCounter = 0;
    int sequenceCursor = 0;
    uint64_t inputOrderCounter = 0;
    bool wasPlayingNotes = false;

    // Ratchet: a musical step can subdivide into N rapid repeats of the SAME note.
    int ratchetIndex = 0;          // 0 = start of a fresh musical step
    int ratchetCountForStep = 1;
    int currentStepVelocity = 96;
    std::vector<int> currentStepNotes;   // notes chosen for the current musical step

    // Host transport tracking for panic/reset/loop-wrap handling.
    bool hostWasPlaying = false;
    double lastPpq = -1.0;

    std::vector<HeldNote> heldNotes;
    std::vector<int> outputNotes;

    // Realtime scratch: reused every block/step so the audio thread never allocates.
    mutable std::vector<int> baseNotesScratch; // intermediate for buildSortedBaseNotes/buildStepNotes
    std::vector<int> stepNotesScratch;         // result of chooseNotesForStep (returned by ref)
    juce::MidiBuffer scratchOutput;            // reused output buffer (avoids per-block MidiBuffer alloc)

    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariArpAudioProcessor)
};
