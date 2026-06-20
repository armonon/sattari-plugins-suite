// Contract test for the shared sattari::dsp::PitchDetector.
//
// Feeds clean sine tones across the vocal range and asserts the detector locks to
// the fundamental within 30 cents (and does NOT fall to a sub-harmonic octave),
// plus checks the octave-fold and chromatic-snap helpers.
#include "sattari_audio_core/SattariPitchDetector.h"
#include "sattari_audio_core/SattariEnvelopeFollower.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace sattari::dsp;

int main()
{
    const double sr = 48000.0;
    const int block = 256;
    int fails = 0;

    for (float hz : { 82.0f, 110.0f, 220.0f, 440.0f, 660.0f, 880.0f })
    {
        PitchDetector d;
        d.prepare (sr, 70.0f, 1000.0f, 0.045f);
        double phase = 0.0;
        std::vector<float> buf ((size_t) block);
        for (int b = 0; b < 24; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                buf[(size_t) i] = 0.5f * (float) std::sin (phase);
                phase += 2.0 * M_PI * hz / sr;
            }
            d.pushBlock (buf.data(), block);
        }
        const auto r = d.analyse (0.3f);
        const float cents = 1200.0f * std::log2 (r.frequencyHz / hz);
        std::printf ("  %.0f Hz -> %.2f Hz (%+.1f cents) conf=%.3f voiced=%d\n",
                     hz, r.frequencyHz, cents, r.confidence, (int) r.voiced);
        if (! r.voiced || std::fabs (cents) > 30.0f)
            ++fails;
    }

    if (std::fabs (foldToOctaveNear (880.0f, 440.0f) - 440.0f) > 0.5f) ++fails;
    if (std::fabs (foldToOctaveNear (110.0f, 440.0f) - 440.0f) > 0.5f) ++fails;
    if (std::fabs (snapHzToSemitone (452.0f) - 440.0f) > 1.0f) ++fails;

    // Scale-snap: D# in C major is equidistant from D and E; ties round down
    // (musical convention — the lower tone reads as more "in the key").
    const int snapped = snapMidiToScale (63 /* D#4 */, 0 /* C */, ScaleId::Major);
    std::printf ("snapMidiToScale(D#4, C major) = %d (expect 62 D4 -- tie down)\n", snapped);
    if (snapped != 62) ++fails;
    // Chromatic = passthrough.
    if (snapMidiToScale (63, 0, ScaleId::ChromaticOff) != 63) ++fails;
    // PentaMaj from F# in C (intervals 0,2,4,7,9): nearest is G (+1).
    if (snapMidiToScale (66, 0, ScaleId::PentaMaj) != 67) ++fails;
    // C# in C major: equidistant C (-1) and D (+1); tie rounds down to C.
    if (snapMidiToScale (61, 0, ScaleId::Major) != 60) ++fails;

    // Envelope follower: rectified DC of 1.0 should settle near 1.0; release is
    // slow, attack is fast -> peak hits quickly.
    sattari::dsp::EnvelopeFollower env;
    env.prepare (48000.0, 5.0f, 80.0f);
    for (int i = 0; i < 4800; ++i) env.process (1.0f);   // 100 ms of DC=1
    std::printf ("envelope after 100 ms of unit input = %.4f (expect >0.99)\n", env.current());
    if (env.current() < 0.99f) ++fails;
    for (int i = 0; i < 480; ++i) env.process (0.0f);    // 10 ms of silence
    std::printf ("envelope 10 ms after silence       = %.4f (expect <0.95)\n", env.current());
    if (env.current() > 0.95f) ++fails;

    std::printf (fails == 0 ? "PitchDetector contract ok\n" : "PitchDetector contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
