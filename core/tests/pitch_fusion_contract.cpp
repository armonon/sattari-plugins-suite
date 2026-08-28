#include <sattari_audio_core/SattariPitchFusion.h>

#include <cmath>
#include <iostream>

namespace
{
double centsApart (double a, double b)
{
    return 1200.0 * std::abs (std::log2 (a / b));
}
}

int main()
{
    using E = sattari::dsp::MonophonicPitchEstimate;
    sattari::dsp::MonophonicPitchFusion fusion;

    const auto agreeing = fusion.fuse (E { 440.0, 0.90, true }, E { 442.0, 0.95, true });
    if (! agreeing.voiced || centsApart (agreeing.frequencyHz, 440.0) > 0.01
        || agreeing.confidence <= 0.90)
        return 1;

    fusion.reset();
    const auto octave = fusion.fuse (E { 220.0, 0.92, true }, E { 440.0, 0.99, true });
    if (! octave.voiced || centsApart (octave.frequencyHz, 220.0) > 1.0)
        return 2;

    fusion.reset();
    const auto conflict = fusion.fuse (E { 220.0, 0.91, true }, E { 293.66, 0.99, true });
    if (! conflict.voiced || centsApart (conflict.frequencyHz, 220.0) > 0.01)
        return 3;

    fusion.reset();
    const auto rescued = fusion.fuse (E {}, E { 330.0, 0.86, true });
    if (! rescued.voiced || centsApart (rescued.frequencyHz, 330.0) > 0.01)
        return 4;

    const auto weak = fusion.fuse (E {}, E { 349.23, 0.60, true });
    if (weak.voiced)
        return 5;

    // A neural-only leap after a stable note is withheld for three frames, then
    // admitted as a possible new onset instead of becoming a permanent lockout.
    fusion.reset();
    (void) fusion.fuse (E { 220.0, 0.90, true }, E {});
    for (int frame = 0; frame < 3; ++frame)
        if (fusion.fuse (E {}, E { 440.0, 0.90, true }).voiced)
            return 6;
    const auto newOnset = fusion.fuse (E {}, E { 440.0, 0.90, true });
    if (! newOnset.voiced || centsApart (newOnset.frequencyHz, 440.0) > 0.01)
        return 7;

    fusion.reset();
    const auto invalid = fusion.fuse (E { std::nan (""), 0.99, true },
                                      E { -10.0, 0.99, true });
    if (invalid.voiced)
        return 8;

    std::cout << "Pitch fusion contract ok\n";
    return 0;
}
