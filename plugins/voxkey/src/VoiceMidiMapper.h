#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PitchDetector.h"
#include <vector>

class VoxKeyVoiceMidiMapper final
{
public:
    VoxKeyVoiceMidiMapper() = default;

    enum class Scale
    {
        chromatic = 0,
        major,
        minor,
        pentatonic
    };

    enum class Mode
    {
        melody = 0,
        bassFollow,
        harmony,
        drumMouth,
        padSoulLayer
    };

    struct Settings
    {
        Scale scale = Scale::chromatic;
        Mode mode = Mode::melody;
        int keyRoot = 0;
        int octaveShift = 0;
        float holdMs = 60.0f;
        float releaseMs = 120.0f;
        float velocityResponse = 0.75f;
        float noteStabilityCents = 45.0f;
        float pitchBendRangeSemis = 2.0f;   // must match the receiving synth's bend range
        float startConfidence = 0.0f;       // confidence gate for *new* note onsets (anti-breath)
        bool pitchBend = false;
        bool glide = false;                 // legato: overlap new note over old (mono modes)
        bool bypass = false;
    };

    struct State
    {
        bool voiced = false;
        int detectedNote = -1;
        int outputRoot = -1;
        juce::String detectedName { "--" };
        juce::String outputName { "--" };
        float inputRms = 0.0f;
        float confidence = 0.0f;
        bool midiActive = false;
    };

    void prepare (double sampleRate);
    void reset (juce::MidiBuffer& midi, int samplePosition = 0);
    void processFrame (const VoxKeyPitchFrame& frame, const Settings& settings, juce::MidiBuffer& midi, int samplePosition);

    State getState() const { return state; }

    static int frequencyToMidiNote (float frequencyHz) noexcept;
    static juce::String midiNoteName (int note);
    static int quantizeToScale (int midiNote, int keyRoot, Scale scale) noexcept;
    static std::vector<int> notesForMode (int rootNote, Mode mode, Scale scale, int keyRoot, int octaveShift);

private:
    void startNotes (const std::vector<int>& notes, int velocity, juce::MidiBuffer& midi, int samplePosition);
    void stopActiveNotes (juce::MidiBuffer& midi, int samplePosition);
    int velocityFromRms (float rms, float response) const noexcept;
    bool shouldRetrigger (int candidateNote, float stabilityCents) const noexcept;

    double currentSampleRate = 44100.0;
    int framesSinceVoiced = 0;
    int framesSinceUnvoiced = 0;
    int lastCandidateNote = -1;
    int stableFrames = 0;
    std::vector<int> activeNotes;
    State state;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxKeyVoiceMidiMapper)
};
