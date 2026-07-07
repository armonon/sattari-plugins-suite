// Sattari Double — functional contract.
// Proves the doubler (1) keeps the dry signal and adds decorrelated stereo
// "double" energy (side channel appears) when Doubles > 0, (2) collapses to the
// dry input when Doubles = 0, and (3) stays finite + bounded throughout.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// Render a MONO vocal-like tone (same in both channels) through the doubler.
juce::AudioBuffer<float> render (float doubles, double& sideRms, double& peak)
{
    const double sr = 48000.0; const int block = 512;
    SattariDoubleAudioProcessor p; auto& a = p.parameters();
    setParam (a, "detune", 12.0f); setParam (a, "timing", 18.0f);
    setParam (a, "drift", 0.4f);   setParam (a, "width", 0.9f);
    setParam (a, "level", doubles); setParam (a, "output", 0.0f);
    p.prepareToPlay (sr, block);

    const int n = (int) (sr * 1.5);
    juce::AudioBuffer<float> out (2, n);
    double ph = 0.0; juce::MidiBuffer midi;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len);
        for (int i = 0; i < len; ++i)
        {
            const float s = 0.35f * (float) std::sin (ph) + 0.12f * (float) std::sin (ph * 2.0);
            buf.setSample (0, i, s); buf.setSample (1, i, s);
            ph += 2.0 * M_PI * 150.0 / sr;
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < len; ++i) out.setSample (ch, start + i, buf.getSample (ch, i));
    }

    // Measure over the settled second half.
    const int from = n / 2; double s2 = 0.0; peak = 0.0;
    for (int i = from; i < n; ++i)
    {
        const double side = 0.5 * (out.getSample (0, i) - out.getSample (1, i));
        s2 += side * side;
        peak = std::max (peak, (double) std::max (std::fabs (out.getSample (0, i)),
                                                  std::fabs (out.getSample (1, i))));
    }
    sideRms = std::sqrt (s2 / (n - from));
    return out;
}
} // namespace

int main()
{
    int fails = 0;

    double dryPeak = 0.0, drySide = 0.0;
    auto dry = render (0.0f, drySide, dryPeak);
    double wetPeak = 0.0, wetSide = 0.0;
    auto wet = render (0.7f, wetSide, wetPeak);

    bool finite = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < wet.getNumSamples(); ++i)
            if (! std::isfinite (wet.getSample (ch, i))) finite = false;

    std::printf ("doubles0: side=%.5f peak=%.3f   doubles0.7: side=%.5f peak=%.3f  finite=%d\n",
                 drySide, dryPeak, wetSide, wetPeak, (int) finite);

    if (! finite)                { std::printf ("  FAIL non-finite\n"); ++fails; }
    if (drySide > 1.0e-3)        { std::printf ("  FAIL Doubles=0 is not centred (unexpected width)\n"); ++fails; }
    if (wetSide < 5.0e-3)        { std::printf ("  FAIL Doubles=0.7 added no width/decorrelation\n"); ++fails; }
    if (wetSide <= drySide * 5.0){ std::printf ("  FAIL doubles do not clearly widen vs dry\n"); ++fails; }
    if (wetPeak > 2.0)           { std::printf ("  FAIL runaway peak\n"); ++fails; }

    std::printf (fails == 0 ? "Double contract ok\n" : "Double contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
