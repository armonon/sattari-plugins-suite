// Sattari Arp — timing contract test.
//
// Verifies that generated note-ons land on a sample-accurate grid. With no
// playhead the arp free-runs at the default 120 BPM. At 48 kHz a 1/16 step is
// 0.25 quarter / (120/60) bps = 0.125 s = exactly 6000 samples, so consecutive
// note-ons must be 6000 samples apart (within rounding).
#include "PluginProcessor.h"
#include <sattari_audio_core/SattariEditorRender.h>

#include <cmath>
#include <cstdio>
#include <vector>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc == 3
        && (juce::String (argv[1]) == "--render-ui"
            || juce::String (argv[1]) == "--render-ui-min"))
    {
        SattariArpAudioProcessor processor;
        const juce::File destination (argv[2]);
        const bool minimumSize = juce::String (argv[1]) == "--render-ui-min";
        const bool ok = sattari::test::renderEditorToPng (
            processor, destination,
            minimumSize ? 820 : 0,
            minimumSize ? 640 : 0);
        std::printf ("%s %s\n", ok ? "rendered" : "FAIL render",
                     destination.getFullPathName().toRawUTF8());
        return ok ? 0 : 1;
    }

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

    // Compass response is explicit, deterministic and safe when no session is
    // present. These pure checks exercise the semantic contract without touching
    // the user's cross-plugin bus file.
    sattari::xplugin::SessionSnapshot context;
    context.valid = true;
    context.hasSceneContext = true;
    context.swing = 1.0f;
    context.energy = 0.8f;
    context.sectionId = 4;

    if (std::abs (sattari::compass::effectiveSwing (
            0.1f, true, context) - 0.65f) > 1.0e-6f
        || sattari::compass::effectiveOctaves (2, true, context) != 3
        || sattari::compass::effectiveVelocity (100, true, context) != 100)
    {
        std::printf ("FAIL: Compass response contract\n");
        return 1;
    }

    // A reduced Key fallback is valid for pitch/tempo but must not masquerade
    // as a Compass scene or drive groove/energy responses.
    context.hasSceneContext = false;
    if (std::abs (sattari::compass::effectiveSwing (
            0.1f, true, context) - 0.1f) > 1.0e-6f
        || sattari::compass::effectiveOctaves (2, true, context) != 2
        || sattari::compass::effectiveVelocity (100, true, context) != 100)
    {
        std::printf ("FAIL: Compass fallback contract\n");
        return 1;
    }

    std::printf ("  compass_response=opt-in/fallback-safe\n");
    return 0;
}
