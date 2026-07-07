// Sattari Space — functional contract.
// Proves (1) the reverb produces a decaying tail after the input stops,
// (2) the tempo-synced delay places an echo at the 1/4-note time (0.5 s at the
// 120 BPM fallback), and (3) with both mixes at 0 the output is the dry signal.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <functional>

namespace
{
void sp (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{ if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); }

// Render. `continuous`=false: a 50 ms burst then silence (for reverb/delay tests).
// `continuous`=true: a steady tone (for the dry-pass test). maxDryErr is tracked
// only after 0.2 s so it ignores juce::Reverb's parameter-smoothing ramp at t=0.
juce::AudioBuffer<float> render (std::function<void(juce::AudioProcessorValueTreeState&)> setup,
                                 double seconds, double& maxDryErr, bool continuous = false)
{
    const double sr = 48000.0; const int block = 512;
    SattariSpaceAudioProcessor p; auto& a = p.parameters();
    setup (a);
    p.prepareToPlay (sr, block);
    const int n = (int) (sr * seconds);
    const int settle = (int) (sr * 0.2);
    juce::AudioBuffer<float> out (2, n);
    double ph = 0.0; juce::MidiBuffer midi; maxDryErr = 0.0;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len), in (2, len);
        for (int i = 0; i < len; ++i)
        {
            const int gi = start + i;
            const float s = continuous ? 0.4f * (float) std::sin (ph)
                                       : (gi < 2400 ? 0.5f * (float) std::sin (ph) : 0.0f);
            buf.setSample (0,i,s); buf.setSample (1,i,s);
            in.setSample (0,i,s); in.setSample (1,i,s);
            ph += 2.0 * M_PI * 300.0 / sr;
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < len; ++i)
        {
            out.setSample (ch, start+i, buf.getSample (ch,i));
            if (start + i >= settle)
                maxDryErr = std::max (maxDryErr, (double) std::fabs (buf.getSample (ch,i) - in.getSample (ch,i)));
        }
    }
    return out;
}

double rmsWindow (const juce::AudioBuffer<float>& b, int from, int to)
{
    double s = 0.0; int c = 0;
    for (int i = from; i < to && i < b.getNumSamples(); ++i) { const double v = b.getSample (0,i); s += v*v; ++c; }
    return c ? std::sqrt (s / c) : 0.0;
}
} // namespace

int main()
{
    int fails = 0;
    double e1 = 0, e2 = 0, e3 = 0;

    // 1) Reverb tail well after the burst (0.6-1.0 s).
    auto rev = render ([] (auto& a){ sp (a,"revmix",0.7f); sp (a,"div",0.0f); sp (a,"delaymix",0.0f); }, 1.2, e1);
    const double tail = rmsWindow (rev, 28800, 48000);
    std::printf ("reverb tail rms=%.5f  dryErr=%.1e\n", tail, e1);
    if (tail < 1.0e-3) { std::printf ("  FAIL no reverb tail\n"); ++fails; }

    // 2) Delay echo around 0.5 s (1/4 at 120 BPM = 24000 samples), no reverb.
    auto dly = render ([] (auto& a){ sp (a,"revmix",0.0f); sp (a,"div",1.0f); sp (a,"delaymix",0.85f); sp (a,"feedback",0.3f); }, 1.5, e2);
    const double atEcho   = rmsWindow (dly, 23500, 24900);
    const double atSilent = rmsWindow (dly, 12000, 13400);   // between burst and echo: quiet
    std::printf ("delay echo@0.5s rms=%.5f  gap rms=%.5f\n", atEcho, atSilent);
    if (atEcho < 1.0e-3)          { std::printf ("  FAIL no delay echo at 1/4 note\n"); ++fails; }
    if (atEcho < atSilent * 3.0)  { std::printf ("  FAIL echo not clearly above the gap\n"); ++fails; }

    // 3) Both mixes 0 -> dry pass-through (continuous tone, measured after settle).
    render ([] (auto& a){ sp (a,"revmix",0.0f); sp (a,"div",0.0f); sp (a,"delaymix",0.0f); sp (a,"output",0.0f); }, 0.6, e3, true);
    std::printf ("dry-pass err=%.1e\n", e3);
    if (e3 > 5.0e-3) { std::printf ("  FAIL not dry pass-through at 0 mix\n"); ++fails; }

    std::printf (fails == 0 ? "Space contract ok\n" : "Space contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
