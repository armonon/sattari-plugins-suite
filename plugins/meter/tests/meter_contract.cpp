// Sattari Meter — functional contract.
// Proves (1) audio passes through bit-exact, (2) true-peak tracks the signal
// peak, (3) correlation reads ~+1 for mono and ~-1 for anti-phase, and (4)
// loudness rises ~6 LUFS when the level doubles.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>

namespace
{
SattariMeterAudioProcessor::Readout run (float amp, bool antiphase, double& passErr)
{
    const double sr = 48000.0; const int block = 512;
    SattariMeterAudioProcessor p;
    p.prepareToPlay (sr, block);

    const int n = (int) (sr * 2.0);
    double ph = 0.0; juce::MidiBuffer midi; passErr = 0.0;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len);
        juce::AudioBuffer<float> in (2, len);
        for (int i = 0; i < len; ++i)
        {
            const float s = amp * (float) std::sin (ph);
            buf.setSample (0, i, s); buf.setSample (1, i, antiphase ? -s : s);
            in.setSample (0, i, buf.getSample (0, i)); in.setSample (1, i, buf.getSample (1, i));
            ph += 2.0 * M_PI * 1000.0 / sr;
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < len; ++i)
                passErr = std::max (passErr, (double) std::fabs (buf.getSample (ch, i) - in.getSample (ch, i)));
    }
    return p.getReadout();
}
} // namespace

int main()
{
    int fails = 0;
    double pe1 = 0, pe2 = 0, pe3 = 0;
    auto full = run (0.5f, false, pe1);
    auto half = run (0.25f, false, pe2);
    auto anti = run (0.5f, true, pe3);

    std::printf ("full: mom=%.1f short=%.1f tp=%.1f corr=%.2f | half: mom=%.1f | anti: corr=%.2f | passErr=%.1e\n",
                 full.momentaryLufs, full.shortLufs, full.truePeakDb, full.correlation,
                 half.momentaryLufs, anti.correlation, std::max ({ pe1, pe2, pe3 }));

    if (std::max ({ pe1, pe2, pe3 }) > 1.0e-6)     { std::printf ("  FAIL not bit-exact pass-through\n"); ++fails; }
    if (std::fabs (full.truePeakDb - (-6.02)) > 1.5){ std::printf ("  FAIL true-peak off (expected ~-6 dB)\n"); ++fails; }
    if (full.correlation < 0.9f)                    { std::printf ("  FAIL mono correlation not ~+1\n"); ++fails; }
    if (anti.correlation > -0.9f)                   { std::printf ("  FAIL anti-phase correlation not ~-1\n"); ++fails; }
    if (! std::isfinite (full.momentaryLufs) || full.momentaryLufs < -40.0f || full.momentaryLufs > 0.0f)
                                                    { std::printf ("  FAIL momentary LUFS implausible\n"); ++fails; }
    if (std::fabs ((full.momentaryLufs - half.momentaryLufs) - 6.02f) > 2.0f)
                                                    { std::printf ("  FAIL halving level did not drop ~6 LUFS\n"); ++fails; }

    std::printf (fails == 0 ? "Meter contract ok\n" : "Meter contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
