// Sattari Pulse — functional contract.
// Proves:
//   (1) depth=0 is an exact pass-through (the pump is transparent when off);
//   (2) depth>0 produces a PERIODIC amplitude modulation at the expected tempo
//       rate — the per-block RMS envelope dips once per cycle, and the dip period
//       matches the 1/4-note time at the 120 BPM fallback (0.5 s);
//   (3) the output is bounded to |input| (the effect can only ever duck);
//   (4) no NaNs / infs ever appear.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace
{
void sp (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{ if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); }

struct Rendered
{
    juce::AudioBuffer<float> out;
    juce::AudioBuffer<float> dry;
    bool sawNaN = false;
};

// Render a steady full-scale tone through Pulse. No host playhead, so BPM falls
// back to 120 and the phase free-runs — exactly the standalone / bus case.
Rendered render (std::function<void(juce::AudioProcessorValueTreeState&)> setup,
                 double seconds)
{
    const double sr = 48000.0; const int block = 512;
    SattariPulseAudioProcessor p; auto& a = p.parameters();
    setup (a);
    p.prepareToPlay (sr, block);

    const int n = (int) (sr * seconds);
    Rendered r;
    r.out.setSize (2, n);
    r.dry.setSize (2, n);
    double ph = 0.0; juce::MidiBuffer midi;

    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len);
        for (int i = 0; i < len; ++i)
        {
            const float s = 0.8f * (float) std::sin (ph);
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
            r.dry.setSample (0, start + i, s);
            r.dry.setSample (1, start + i, s);
            ph += 2.0 * M_PI * 220.0 / sr;
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < len; ++i)
            {
                const float v = buf.getSample (ch, i);
                if (std::isnan (v) || std::isinf (v)) r.sawNaN = true;
                r.out.setSample (ch, start + i, v);
            }
    }
    return r;
}

// RMS of channel 0 over [from,to).
double rmsWindow (const juce::AudioBuffer<float>& b, int from, int to)
{
    double s = 0.0; int c = 0;
    for (int i = from; i < to && i < b.getNumSamples(); ++i)
    { const double v = b.getSample (0, i); s += v * v; ++c; }
    return c ? std::sqrt (s / c) : 0.0;
}

// Max abs error out-vs-dry over the whole buffer.
double maxAbsErr (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& dry)
{
    double e = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
            e = std::max (e, (double) std::fabs (out.getSample (ch, i) - dry.getSample (ch, i)));
    return e;
}
} // namespace

int main()
{
    int fails = 0;
    const double sr = 48000.0;

    // 1) depth=0 -> exact pass-through (mix full, so only depth gates the effect).
    {
        auto r = render ([] (auto& a)
        { sp (a, "shape", 0.0f); sp (a, "rate", 2.0f); sp (a, "depth", 0.0f);
          sp (a, "smooth", 0.0f); sp (a, "filter", 0.0f); sp (a, "mix", 1.0f); }, 0.5);
        const double err = maxAbsErr (r.out, r.dry);
        std::printf ("depth=0 pass-through err=%.2e  nan=%d\n", err, (int) r.sawNaN);
        if (err > 1.0e-5) { std::printf ("  FAIL depth=0 not pass-through\n"); ++fails; }
        if (r.sawNaN)     { std::printf ("  FAIL NaN in pass-through\n"); ++fails; }
    }

    // 2) depth>0, Sine shape, 1/4 rate @120 BPM -> the RMS envelope dips once per
    //    0.5 s. Measure per-window RMS across 2 s and confirm a clear periodic dip
    //    whose period matches ~0.5 s (24000 samples).
    {
        auto r = render ([] (auto& a)
        { sp (a, "shape", 0.0f); sp (a, "rate", 2.0f); sp (a, "depth", 1.0f);
          sp (a, "smooth", 0.0f); sp (a, "filter", 0.0f); sp (a, "mix", 1.0f); }, 2.0);
        if (r.sawNaN) { std::printf ("  FAIL NaN in modulated render\n"); ++fails; }

        // Coarse RMS envelope: one value per ~5 ms window.
        const int win = (int) (sr * 0.005);
        std::vector<double> env;
        for (int s = 0; s + win <= r.out.getNumSamples(); s += win)
            env.push_back (rmsWindow (r.out, s, s + win));

        double lo = 1e9, hi = 0.0;
        for (double v : env) { lo = std::min (lo, v); hi = std::max (hi, v); }
        std::printf ("modulation env lo=%.4f hi=%.4f ratio=%.2f\n", lo, hi, lo > 0 ? hi / lo : 0.0);
        // A real Sine pump: near-silent on the beat, near-full off it.
        if (! (hi > 0.15))          { std::printf ("  FAIL no signal peaks\n"); ++fails; }
        if (! (lo < hi * 0.25))     { std::printf ("  FAIL envelope not clearly ducking\n"); ++fails; }

        // Period check: find the sample index of the deepest dip in the first and
        // second cycles; they should be ~24000 samples (0.5 s) apart.
        auto dipIn = [&] (int fromWin, int toWin)
        {
            int idx = fromWin; double best = 1e9;
            for (int w = fromWin; w < toWin && w < (int) env.size(); ++w)
                if (env[(size_t) w] < best) { best = env[(size_t) w]; idx = w; }
            return idx;
        };
        const int winsPerCycle = (int) std::round (0.5 / 0.005);   // 100 windows / cycle
        const int dip1 = dipIn (winsPerCycle / 2, winsPerCycle + winsPerCycle / 2);
        const int dip2 = dipIn (winsPerCycle + winsPerCycle / 2, 2 * winsPerCycle + winsPerCycle / 2);
        const double periodSamples = (double) (dip2 - dip1) * win;
        std::printf ("dip period=%.0f samples (expect ~24000)\n", periodSamples);
        if (std::fabs (periodSamples - 24000.0) > 4000.0)
        { std::printf ("  FAIL modulation period off the 1/4-note grid\n"); ++fails; }
    }

    // 3) Output bounded to |input|: with any shape/depth the effect only attenuates,
    //    so |out| must never exceed |dry| (allow a tiny epsilon for the filter path).
    {
        auto r = render ([] (auto& a)
        { sp (a, "shape", 3.0f); sp (a, "rate", 4.0f); sp (a, "depth", 1.0f);
          sp (a, "smooth", 0.5f); sp (a, "filter", 0.8f); sp (a, "mix", 1.0f); }, 1.0);
        bool exceeded = false; double worst = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < r.out.getNumSamples(); ++i)
            {
                const double a = std::fabs (r.out.getSample (ch, i));
                const double d = std::fabs (r.dry.getSample (ch, i));
                worst = std::max (worst, a - d);
                if (a > d + 1.0e-3) exceeded = true;
            }
        std::printf ("bound check worst(|out|-|dry|)=%.2e  nan=%d\n", worst, (int) r.sawNaN);
        if (exceeded) { std::printf ("  FAIL output exceeded input magnitude\n"); ++fails; }
        if (r.sawNaN) { std::printf ("  FAIL NaN in gated render\n"); ++fails; }
    }

    // 4) Spike + full filter + no smoothing: stress path, must stay finite.
    {
        auto r = render ([] (auto& a)
        { sp (a, "shape", 4.0f); sp (a, "rate", 6.0f); sp (a, "depth", 1.0f);
          sp (a, "smooth", 0.0f); sp (a, "filter", 1.0f); sp (a, "mix", 1.0f); }, 0.5);
        if (r.sawNaN) { std::printf ("  FAIL NaN in spike stress\n"); ++fails; }
        else          std::printf ("spike stress finite ok\n");
    }

    std::printf (fails == 0 ? "Pulse contract ok\n" : "Pulse contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
