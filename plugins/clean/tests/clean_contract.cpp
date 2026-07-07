// Sattari Clean — functional contract.
// Feeds constant broadband noise plus periodic tone bursts. Proves that with
// Amount up, the noise-only GAPS are pushed down (denoised) while the tone BURSTS
// are preserved, and output stays finite.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>

namespace
{
void sp (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{ if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); }

void render (float amount, double& gapRms, double& burstRms, bool& finite)
{
    const double sr = 48000.0; const int block = 512;
    const int period = (int) (sr * 0.5);          // burst every 0.5 s
    const int burstLen = (int) (sr * 0.2);
    SattariCleanAudioProcessor p; auto& a = p.parameters();
    sp (a, "amount", amount); sp (a, "sensitivity", 4.0f); sp (a, "output", 0.0f);
    p.prepareToPlay (sr, block);

    const int n = period * 6;
    juce::AudioBuffer<float> out (2, n);
    juce::uint32 rng = 0x2545F491;
    auto noise = [&rng] { rng = rng * 1664525u + 1013904223u; return ((float) rng / (float) 0xFFFFFFFFu) * 2.0f - 1.0f; };
    double ph = 0.0; juce::MidiBuffer midi; finite = true;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len);
        for (int i = 0; i < len; ++i)
        {
            const int gi = start + i;
            const bool inBurst = (gi % period) < burstLen;
            float s = 0.03f * noise();
            if (inBurst) s += 0.4f * (float) std::sin (ph);
            buf.setSample (0,i,s); buf.setSample (1,i,s);
            ph += 2.0 * M_PI * 300.0 / sr;
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < len; ++i)
        { const float v = buf.getSample (ch,i); if (! std::isfinite (v)) finite = false; out.setSample (ch, start+i, v); }
    }

    // Use the last cycle (floor adapted). Burst window inside a burst; gap window
    // deep in a silence gap.
    auto rms = [&] (int from, int to) { double s=0; int c=0; for (int i=from;i<to;++i){ const double v=out.getSample(0,i); s+=v*v; ++c;} return c?std::sqrt(s/c):0.0; };
    const int lastBurst = period * 5;
    burstRms = rms (lastBurst + (int)(sr*0.08), lastBurst + (int)(sr*0.18));
    gapRms   = rms (lastBurst + burstLen + (int)(sr*0.12), lastBurst + burstLen + (int)(sr*0.28));
}
} // namespace

int main()
{
    int fails = 0;
    bool f0 = true, f1 = true;
    double gap0=0, burst0=0, gap1=0, burst1=0;
    render (0.0f, gap0, burst0, f0);
    render (0.85f, gap1, burst1, f1);

    std::printf ("amount0:   gap=%.5f burst=%.4f\n", gap0, burst0);
    std::printf ("amount.85: gap=%.5f burst=%.4f\n", gap1, burst1);

    if (! (f0 && f1))            { std::printf ("  FAIL non-finite\n"); ++fails; }
    if (gap1 > gap0 * 0.6)       { std::printf ("  FAIL gap noise not reduced enough\n"); ++fails; }
    if (burst1 < burst0 * 0.7)   { std::printf ("  FAIL tone burst not preserved\n"); ++fails; }

    std::printf (fails == 0 ? "Clean contract ok\n" : "Clean contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
