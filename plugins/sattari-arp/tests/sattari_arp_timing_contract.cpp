// Sattari Arp — timing contract test.
//
// Verifies that generated note-ons land on a sample-accurate grid. With no
// playhead the arp free-runs at the default 120 BPM. At 48 kHz a 1/16 step is
// 0.25 quarter / (120/60) bps = 0.125 s = exactly 6000 samples, so consecutive
// note-ons must be 6000 samples apart (within rounding).
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    SattariArpAudioProcessor proc;
    const double sr    = 48000.0;
    const int    block = 512;
    proc.prepareToPlay (sr, block);

    // Defaults: rate=1/16, pattern Up, octaves 1, swing 0, ratchet 1, scale Off.
    const int expectedSpacing = 6000;

    juce::AudioBuffer<float> buffer (1, block);
    std::vector<int> noteOnSamples;

    const int numBlocks = 80;
    for (int b = 0; b < numBlocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);

        proc.processBlock (buffer, midi);

        for (const auto m : midi)
            if (m.getMessage().isNoteOn())
                noteOnSamples.push_back (b * block + m.samplePosition);
    }

    if (noteOnSamples.size() < 4)
    {
        std::printf ("FAIL: too few note-ons (%zu)\n", noteOnSamples.size());
        return 1;
    }

    int maxErr = 0;
    for (size_t i = 1; i < noteOnSamples.size(); ++i)
        maxErr = std::max (maxErr, std::abs ((noteOnSamples[i] - noteOnSamples[i - 1]) - expectedSpacing));

    std::printf ("Sattari Arp timing contract ok\n");
    std::printf ("  note_ons=%zu  expected_spacing=%d  max_spacing_error=%d samples\n",
                 noteOnSamples.size(), expectedSpacing, maxErr);

    if (maxErr > 1)
    {
        std::printf ("FAIL: spacing error exceeds 1 sample\n");
        return 1;
    }
    return 0;
}
