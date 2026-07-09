// Sattari Heat — functional contract.
// Proves: (1) mix=0 is latency-comped pass-through and drive=0 @ unity is ~
// transparent; (2) a hot signal driven hard is saturated (RMS compresses vs a
// clean linear reference and adds harmonics) and stays bounded <= 1; (3) every
// model runs finite; (4) no NaNs anywhere.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// Render a sine at `amp` through Heat with the given settings; return the
// output block AND the exact input, so the caller can compare with the
// reported latency removed.
struct RenderOut
{
    std::vector<float> in, out;
    double peak = 0.0;
    bool   finite = true;
    int    latency = 0;
};

RenderOut render (int model, float driveDb, float outDb, float mix, float bias,
                  float amp, double freq)
{
    const double sr = 48000.0; const int block = 256;
    const int n = 48000; // 1 s
    SattariHeatAudioProcessor p; auto& a = p.parameters();
    setParam (a, "model", (float) model);
    setParam (a, "drive", driveDb);
    setParam (a, "output", outDb);
    setParam (a, "mix", mix);
    setParam (a, "bias", bias);
    setParam (a, "tone", 0.0f);
    p.prepareToPlay (sr, block);

    RenderOut r;
    r.latency = p.getLatencySamples();
    r.in.resize ((size_t) n);
    r.out.resize ((size_t) n);

    double ph = 0.0; juce::MidiBuffer midi;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len);
        for (int i = 0; i < len; ++i)
        {
            const float s = amp * (float) std::sin (ph);
            buf.setSample (0, i, s); buf.setSample (1, i, s);
            r.in[(size_t) (start + i)] = s;
            ph += 2.0 * M_PI * freq / sr;
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < len; ++i)
        {
            const float v = buf.getSample (0, i);
            if (! std::isfinite (v)) r.finite = false;
            r.out[(size_t) (start + i)] = v;
            r.peak = std::max (r.peak, (double) std::fabs (v));
        }
    }
    return r;
}

// Mean-square of the steady-state region (skip the head to let filters settle).
double rms (const std::vector<float>& v, int from, int to)
{
    double s2 = 0.0; int c = 0;
    for (int i = from; i < to && i < (int) v.size(); ++i)
    {
        const double x = (double) v[(size_t) i];
        s2 += x * x; ++c;
    }
    return c ? std::sqrt (s2 / c) : 0.0;
}

// Max |out[i+lat] - in[i]| over the steady region — pass-through error.
double passthroughErr (const RenderOut& r)
{
    double e = 0.0;
    const int lat = r.latency;
    const int n = (int) r.in.size();
    for (int i = 4000; i + lat < n; ++i)
        e = std::max (e, (double) std::fabs (r.out[(size_t) (i + lat)] - r.in[(size_t) i]));
    return e;
}
} // namespace

int main()
{
    int fails = 0;

    // (1a) mix=0 → latency-comped pure dry, regardless of drive. Push drive hard
    //      to prove the wet path is fully removed when mix=0.
    {
        auto r = render (2, 30.0f, 0.0f, /*mix*/ 0.0f, 0.0f, 0.5f, 220.0);
        const double e = passthroughErr (r);
        std::printf ("mix=0 passthrough err = %.6f (latency %d)\n", e, r.latency);
        if (! r.finite)  { std::printf ("  FAIL mix=0 non-finite\n"); ++fails; }
        if (e > 1.0e-4)  { std::printf ("  FAIL mix=0 is not pass-through\n"); ++fails; }
    }

    // (1b) drive=0 @ unity output, full wet → essentially transparent. The shaper
    //      at unity pre-gain is near-linear; allow a small tolerance for the
    //      oversampler's band-limiting and drive compensation.
    {
        auto r = render (0, 0.0f, 0.0f, /*mix*/ 1.0f, 0.0f, 0.3f, 220.0);
        const double e = passthroughErr (r);
        std::printf ("drive=0 wet transparency err = %.6f\n", e);
        if (! r.finite) { std::printf ("  FAIL drive=0 non-finite\n"); ++fails; }
        if (e > 0.05)   { std::printf ("  FAIL drive=0 unity not transparent\n"); ++fails; }
    }

    // (2) Hot signal, hard drive → saturation compresses RMS vs a matched linear
    //     reference AND stays bounded <= ~1. Reference: the same input scaled by
    //     the same output trim but with NO shaping (drive 0). We compare wet RMS
    //     against that linear-through-the-same-trim RMS.
    for (int m = 0; m < 4; ++m)
    {
        // Linear reference: drive 0, unity, full wet.
        auto lin = render (m, 0.0f, 0.0f, 1.0f, 0.0f, 0.9f, 180.0);
        // Driven: same amp, hot drive, output pulled to keep levels comparable.
        auto sat = render (m, 24.0f, -12.0f, 1.0f, 0.2f, 0.9f, 180.0);

        const double linR = rms (lin.out, 4000, 44000);
        const double satR = rms (sat.out, 4000, 44000);
        std::printf ("model %d: linRMS=%.4f satRMS=%.4f peak=%.4f\n", m, linR, satR, sat.peak);

        if (! sat.finite)        { std::printf ("  FAIL model %d non-finite\n", m); ++fails; }
        if (sat.peak > 1.001)    { std::printf ("  FAIL model %d exceeded ceiling\n", m); ++fails; }
        // Saturated + level-matched output should be no louder than the clean
        // reference (the shaper compresses peaks/crest, ceiling caps the rest).
        if (satR > linR * 1.02)  { std::printf ("  FAIL model %d did not compress\n", m); ++fails; }
        if (satR < 1.0e-4)       { std::printf ("  FAIL model %d produced silence\n", m); ++fails; }
    }

    // (3) Harmonic generation: a driven pure tone must gain energy at 2f/3f that
    //     the clean tone does not have. Measure via a crude Goertzel at 3f.
    {
        const double sr = 48000.0, f = 150.0;
        auto clean = render (0, 0.0f, 0.0f, 1.0f, 0.0f, 0.8f, f);
        auto hot   = render (0, 20.0f, 0.0f, 1.0f, 0.3f, 0.8f, f);
        auto goertzel = [sr] (const std::vector<float>& v, double freq) -> double
        {
            const int from = 4000, to = 44000;
            const double w = 2.0 * M_PI * freq / sr;
            const double cw = std::cos (w);
            double s0 = 0, s1 = 0, s2 = 0;
            for (int i = from; i < to; ++i)
            {
                s0 = v[(size_t) i] + 2.0 * cw * s1 - s2;
                s2 = s1; s1 = s0;
            }
            return std::sqrt (s1 * s1 + s2 * s2 - 2.0 * cw * s1 * s2);
        };
        const double h3clean = goertzel (clean.out, 3.0 * f);
        const double h3hot   = goertzel (hot.out,   3.0 * f);
        std::printf ("3rd-harmonic: clean=%.2f hot=%.2f\n", h3clean, h3hot);
        if (h3hot <= h3clean * 3.0)
        { std::printf ("  FAIL drive did not add harmonics\n"); ++fails; }
    }

    std::printf (fails == 0 ? "Heat contract ok\n" : "Heat contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
