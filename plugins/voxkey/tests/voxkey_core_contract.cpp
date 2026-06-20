#include "PitchDetector.h"
#include "VoiceMidiMapper.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
void fail (const char* message)
{
    std::cerr << "VoxKey contract failed: " << message << "\n";
    std::exit (1);
}

std::vector<float> sineBlock (double sampleRate, float frequency, int numSamples, double& phase, float gain = 0.25f)
{
    std::vector<float> block ((size_t) numSamples);
    const double step = juce::MathConstants<double>::twoPi * (double) frequency / sampleRate;
    for (int i = 0; i < numSamples; ++i)
    {
        block[(size_t) i] = (float) (gain * std::sin (phase));
        phase += step;
        if (phase > juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;
    }
    return block;
}

int countNoteOns (const juce::MidiBuffer& midi)
{
    int count = 0;
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOn())
            ++count;
    return count;
}

bool hasNoteOn (const juce::MidiBuffer& midi, int note)
{
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOn() && metadata.getMessage().getNoteNumber() == note)
            return true;
    return false;
}
}

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    VoxKeyPitchDetector detector;
    detector.prepare (sampleRate, blockSize);

    VoxKeyPitchFrame frame;
    double phase = 0.0;
    for (int i = 0; i < 20; ++i)
    {
        const auto block = sineBlock (sampleRate, 440.0f, blockSize, phase);
        frame = detector.analyseBlock (block.data(), blockSize, 0.7f, 0.45f, 0.15f);
    }

    if (! frame.voiced)
        fail ("YIN did not detect voiced A4 sine");

    if (std::abs (frame.frequencyHz - 440.0f) > 6.0f)
    {
        std::cerr << "detected_frequency=" << frame.frequencyHz << " confidence=" << frame.confidence << "\n";
        fail ("YIN frequency outside tolerance for A4");
    }

    if (VoxKeyVoiceMidiMapper::frequencyToMidiNote (440.0f) != 69)
        fail ("440 Hz did not map to MIDI A4");

    if (VoxKeyVoiceMidiMapper::quantizeToScale (61, 0, VoxKeyVoiceMidiMapper::Scale::major) != 60)
        fail ("C# did not quantize down to C in C major");

    VoxKeyVoiceMidiMapper mapper;
    mapper.prepare (sampleRate);
    VoxKeyVoiceMidiMapper::Settings settings;
    settings.scale = VoxKeyVoiceMidiMapper::Scale::chromatic;
    settings.mode = VoxKeyVoiceMidiMapper::Mode::melody;
    settings.holdMs = 10.0f;
    settings.releaseMs = 40.0f;

    juce::MidiBuffer midi;
    mapper.processFrame (frame, settings, midi, 0);
    if (! hasNoteOn (midi, 69))
        fail ("Melody mode did not emit A4 note-on");

    midi.clear();
    settings.mode = VoxKeyVoiceMidiMapper::Mode::harmony;
    mapper.processFrame (frame, settings, midi, 0);
    if (countNoteOns (midi) < 3)
        fail ("Harmony mode did not emit chord note-ons");

    midi.clear();
    settings.mode = VoxKeyVoiceMidiMapper::Mode::bassFollow;
    mapper.processFrame (frame, settings, midi, 0);
    if (! hasNoteOn (midi, 45))
        fail ("Bass Follow mode did not emit two-octave-lower A");

    midi.clear();
    mapper.reset (midi, 0);
    for (const auto metadata : midi)
        if (! metadata.getMessage().isNoteOff())
            fail ("Reset emitted non-note-off MIDI");

    std::cout << "VoxKey core contract ok\n"
              << "  detected_frequency=" << frame.frequencyHz << "\n"
              << "  confidence=" << frame.confidence << "\n"
              << "  melody_note=A4\n"
              << "  modes=melody,harmony,bass_follow\n";
    return 0;
}
