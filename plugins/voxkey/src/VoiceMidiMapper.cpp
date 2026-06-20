#include "VoiceMidiMapper.h"
#include <sattari_audio_core/SattariPitchDetector.h>

namespace
{
constexpr int midiChannel = 1;
const juce::StringArray noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

float midiNoteToFrequency (int note) noexcept
{
    return sattari::dsp::midiToHz ((float) note);
}

// Map VoxKey's 4-value enum to the suite-wide ScaleId.
sattari::dsp::ScaleId toScaleId (VoxKeyVoiceMidiMapper::Scale s) noexcept
{
    switch (s)
    {
        case VoxKeyVoiceMidiMapper::Scale::major:      return sattari::dsp::ScaleId::Major;
        case VoxKeyVoiceMidiMapper::Scale::minor:      return sattari::dsp::ScaleId::Minor;
        case VoxKeyVoiceMidiMapper::Scale::pentatonic: return sattari::dsp::ScaleId::PentaMaj;
        case VoxKeyVoiceMidiMapper::Scale::chromatic:
        default:                                       return sattari::dsp::ScaleId::ChromaticOff;
    }
}

void addPitchBendForVibrato (float sourceFrequencyHz, int targetNote, float rangeSemis,
                             juce::MidiBuffer& midi, int samplePosition)
{
    if (sourceFrequencyHz <= 0.0f || ! juce::isPositiveAndBelow (targetNote, 128))
        return;

    // Map the sung-vs-target cents onto the receiving synth's configured bend range.
    const float rangeCents = juce::jmax (1.0f, rangeSemis) * 100.0f;
    const float targetHz = midiNoteToFrequency (targetNote);
    const float cents = 1200.0f * std::log2 (sourceFrequencyHz / targetHz);
    const float normalized = juce::jlimit (-1.0f, 1.0f, cents / rangeCents);
    const int wheel = juce::jlimit (0, 16383, (int) std::lround (8192.0f + normalized * 8191.0f));
    midi.addEvent (juce::MidiMessage::pitchWheel (midiChannel, wheel), samplePosition);
}

}

void VoxKeyVoiceMidiMapper::prepare (double sampleRate)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    juce::ignoreUnused (currentSampleRate);
    activeNotes.clear();
    framesSinceVoiced = 0;
    framesSinceUnvoiced = 0;
    lastCandidateNote = -1;
    stableFrames = 0;
    state = {};
}

void VoxKeyVoiceMidiMapper::reset (juce::MidiBuffer& midi, int samplePosition)
{
    stopActiveNotes (midi, samplePosition);
    framesSinceVoiced = 0;
    framesSinceUnvoiced = 0;
    lastCandidateNote = -1;
    stableFrames = 0;
    state = {};
}

void VoxKeyVoiceMidiMapper::processFrame (const VoxKeyPitchFrame& frame, const Settings& settings, juce::MidiBuffer& midi, int samplePosition)
{
    state.inputRms = frame.rms;
    state.confidence = frame.confidence;
    state.voiced = frame.voiced;

    if (settings.bypass)
    {
        reset (midi, samplePosition);
        return;
    }

    if (! frame.voiced)
    {
        ++framesSinceUnvoiced;
        framesSinceVoiced = 0;
        const int releaseFrames = juce::jlimit (1, 40, (int) std::round (settings.releaseMs / 20.0f));
        if (framesSinceUnvoiced >= releaseFrames)
        {
            stopActiveNotes (midi, samplePosition);
            state.detectedNote = -1;
            state.outputRoot = -1;
            state.detectedName = "--";
            state.outputName = "--";
        }
        state.midiActive = ! activeNotes.empty();
        return;
    }

    framesSinceUnvoiced = 0;
    ++framesSinceVoiced;

    const int rawNote = frequencyToMidiNote (frame.frequencyHz);
    const int scaleNote = quantizeToScale (rawNote, settings.keyRoot, settings.scale);
    state.detectedNote = rawNote;
    state.detectedName = midiNoteName (rawNote);

    if (rawNote < 0)
        return;

    if (scaleNote != lastCandidateNote)
    {
        lastCandidateNote = scaleNote;
        stableFrames = 1;
    }
    else
    {
        ++stableFrames;
    }

    const int holdFrames = juce::jlimit (1, 30, (int) std::round (settings.holdMs / 20.0f));
    if (stableFrames < holdFrames && ! activeNotes.empty())
    {
        state.midiActive = true;
        return;
    }

    const auto notes = notesForMode (scaleNote, settings.mode, settings.scale, settings.keyRoot, settings.octaveShift);
    if (notes.empty())
        return;

    state.outputRoot = notes.front();
    state.outputName = midiNoteName (notes.front());

    if (! shouldRetrigger (notes.front(), settings.noteStabilityCents) && activeNotes == notes)
    {
        if (settings.pitchBend && settings.mode != Mode::harmony && settings.mode != Mode::drumMouth)
            addPitchBendForVibrato (frame.frequencyHz, activeNotes.front(), settings.pitchBendRangeSemis, midi, samplePosition);
        state.midiActive = true;
        return;
    }

    // Anti-breath gate: a brand-new onset (from silence) must clear the start
    // confidence; note *changes* while already singing pass through so legato works.
    if (activeNotes.empty() && frame.confidence < settings.startConfidence)
        return;

    const int velocity = velocityFromRms (frame.rms, settings.velocityResponse);
    const bool legato = settings.glide && activeNotes.size() == 1 && notes.size() == 1;
    if (legato)
    {
        // Overlap: sound the new note before releasing the old one, so a mono
        // synth glides (portamento) instead of retriggering its amp envelope.
        const int previous = activeNotes.front();
        midi.addEvent (juce::MidiMessage::noteOn (midiChannel, notes.front(),
                                                  (juce::uint8) juce::jlimit (1, 127, velocity)), samplePosition);
        midi.addEvent (juce::MidiMessage::noteOff (midiChannel, previous), samplePosition);
        activeNotes = notes;
        state.midiActive = true;
    }
    else
    {
        stopActiveNotes (midi, samplePosition);
        startNotes (notes, velocity, midi, samplePosition);
    }

    if (settings.pitchBend && settings.mode != Mode::harmony && settings.mode != Mode::drumMouth)
        midi.addEvent (juce::MidiMessage::pitchWheel (midiChannel, 8192), samplePosition);

    state.midiActive = ! activeNotes.empty();
}

int VoxKeyVoiceMidiMapper::frequencyToMidiNote (float frequencyHz) noexcept
{
    if (frequencyHz <= 0.0f)
        return -1;
    return juce::jlimit (0, 127, sattari::dsp::nearestMidiNote (frequencyHz));
}

juce::String VoxKeyVoiceMidiMapper::midiNoteName (int note)
{
    if (! juce::isPositiveAndBelow (note, 128))
        return "--";

    const int octave = note / 12 - 1;
    return noteNames[note % 12] + juce::String (octave);
}

int VoxKeyVoiceMidiMapper::quantizeToScale (int midiNote, int keyRoot, Scale scale) noexcept
{
    if (! juce::isPositiveAndBelow (midiNote, 128))
        return midiNote;
    return sattari::dsp::snapMidiToScale (midiNote, keyRoot, toScaleId (scale));
}

std::vector<int> VoxKeyVoiceMidiMapper::notesForMode (int rootNote, Mode mode, Scale scale, int keyRoot, int octaveShift)
{
    if (! juce::isPositiveAndBelow (rootNote, 128))
        return {};

    const int shifted = juce::jlimit (0, 127, rootNote + octaveShift * 12);
    switch (mode)
    {
        case Mode::melody:
            return { shifted };

        case Mode::bassFollow:
            return { juce::jlimit (0, 127, rootNote - 24 + octaveShift * 12) };

        case Mode::harmony:
        {
            const int third = quantizeToScale (shifted + (scale == Scale::minor ? 3 : 4), keyRoot, scale);
            const int fifth = quantizeToScale (shifted + 7, keyRoot, scale);
            return { shifted, third, fifth };
        }

        case Mode::drumMouth:
        {
            const int degree = ((rootNote - keyRoot) % 12 + 12) % 12;
            if (degree <= 2) return { 36 };       // boom / low mouth hit -> kick
            if (degree <= 5) return { 38 };       // ka -> snare
            if (degree <= 8) return { 42 };       // ts -> closed hat
            return { 46 };                        // bright ts/ss -> open hat
        }

        case Mode::padSoulLayer:
        {
            const int root = juce::jlimit (0, 127, rootNote - 12 + octaveShift * 12);
            const int fifth = quantizeToScale (root + 7, keyRoot, scale);
            return { root, fifth };
        }
    }

    return { shifted };
}

void VoxKeyVoiceMidiMapper::startNotes (const std::vector<int>& notes, int velocity, juce::MidiBuffer& midi, int samplePosition)
{
    activeNotes = notes;
    for (int note : activeNotes)
        midi.addEvent (juce::MidiMessage::noteOn (midiChannel, note, (juce::uint8) juce::jlimit (1, 127, velocity)), samplePosition);
}

void VoxKeyVoiceMidiMapper::stopActiveNotes (juce::MidiBuffer& midi, int samplePosition)
{
    for (int note : activeNotes)
        midi.addEvent (juce::MidiMessage::noteOff (midiChannel, note), samplePosition);
    activeNotes.clear();
    state.midiActive = false;
}

int VoxKeyVoiceMidiMapper::velocityFromRms (float rms, float response) const noexcept
{
    const float scaled = juce::jlimit (0.0f, 1.0f, rms * juce::jmap (response, 0.0f, 1.0f, 18.0f, 75.0f));
    return juce::jlimit (1, 127, 18 + (int) std::round (scaled * 109.0f));
}

bool VoxKeyVoiceMidiMapper::shouldRetrigger (int candidateNote, float stabilityCents) const noexcept
{
    juce::ignoreUnused (stabilityCents);
    return activeNotes.empty() || activeNotes.front() != candidateNote;
}
