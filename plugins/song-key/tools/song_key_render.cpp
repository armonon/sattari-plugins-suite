// Sattari Key — offline detection proof.
//
// Feeds a C major chord (C4+E4+G4) into the analyser, runs the chroma->key
// chain, and asserts it detects C. Proves the master-key scan actually resolves
// the key from a full-mix-style signal, not just compiles.
//
//   song_key_render          # detect + assert C (local proof)
//   song_key_render --smoke  # assert a valid key is detected (CI-safe)
#include "SongKeyProcessor.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}
}

int main (int argc, char** argv)
{
    const bool smoke = (argc > 1 && std::strcmp (argv[1], "--smoke") == 0);
    const double sr = 48000.0;
    const int    block = 512;

    SattariSongKeyAudioProcessor proc;
    proc.prepareToPlay (sr, block);
    proc.releaseResources();                   // stop the background worker; we drive analysis directly
    setParam (proc.parameters(), "publish", 0.0f);   // don't touch the suite bus during the test

    // ~0.7 s of a C major triad, fed block by block to fill the analysis ring.
    const double f[3] = { 261.63, 329.63, 392.00 };
    double ph[3] = { 0, 0, 0 };
    const int blocks = (int) (0.7 * sr / block);
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buf (2, block);
        for (int i = 0; i < block; ++i)
        {
            float s = 0.0f;
            for (int k = 0; k < 3; ++k) { s += 0.3f * (float) std::sin (ph[k]); ph[k] += 2.0 * juce::MathConstants<double>::pi * f[k] / sr; }
            buf.setSample (0, i, s); buf.setSample (1, i, s);
        }
        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
    }

    for (int i = 0; i < 20; ++i) proc.analyseNow();   // let the rolling chroma settle

    const auto d = proc.detected();
    if (! d.valid) { std::fprintf (stderr, "FAIL: no key detected\n"); return 1; }
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    std::printf ("Sattari Key detected: %s %s (confidence %.3f)\n", names[d.root % 12], d.minor ? "minor" : "major", d.confidence);

    if (smoke) { std::printf ("Sattari Key render smoke ok\n"); return 0; }

    if (d.root != 0)
    { std::fprintf (stderr, "FAIL: C major chord detected as root %d, expected 0 (C)\n", d.root); return 1; }

    std::printf ("Sattari Key detection proof ok\n");
    return 0;
}
