// Sattari Echo — functional contract.
// Proves:
//  (1) mix=0 is an exact dry pass-through (with unity output).
//  (2) an impulse produces spaced, DECAYING repeats at the synced time
//      (1/8 at the 120 BPM fallback = 0.25 s = 12000 samples @ 48 kHz),
//      and the second repeat is clearly quieter than the first (feedback<1).
//  (3) feedback near the maximum stays BOUNDED (no runaway) — with tone and
//      diffusion engaged too — and produces no NaN/Inf anywhere.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <functional>

namespace
{
void sp (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{ if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); }

// Render `seconds` of audio. `impulse`=true feeds a single unit spike at t=0 then
// silence (for the repeat / decay tests); `impulse`=false feeds a steady tone
// (for the dry-pass test). Tracks the max |wet-dry| after 0.05 s (dryErr), the
// peak absolute output (for the boundedness test), and whether any sample was
// non-finite.
juce::AudioBuffer<float> render (std::function<void(juce::AudioProcessorValueTreeState&)> setup,
                                 double seconds, double& maxDryErr, double& peak,
                                 bool& sawNaN, bool impulse)
{
    const double sr = 48000.0; const int block = 512;
    SattariEchoAudioProcessor p; auto& a = p.parameters();
    setup (a);
    p.prepareToPlay (sr, block);
    const int n = (int) (sr * seconds);
    const int settle = (int) (sr * 0.05);
    juce::AudioBuffer<float> out (2, n);
    double ph = 0.0; juce::MidiBuffer midi;
    maxDryErr = 0.0; peak = 0.0; sawNaN = false;

    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len), in (2, len);
        for (int i = 0; i < len; ++i)
        {
            const int gi = start + i;
            float s;
            if (impulse)      s = (gi == 0) ? 1.0f : 0.0f;
            else            { s = 0.4f * (float) std::sin (ph); ph += 2.0 * M_PI * 220.0 / sr; }
            buf.setSample (0,i,s); buf.setSample (1,i,s);
            in.setSample  (0,i,s); in.setSample  (1,i,s);
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < len; ++i)
        {
            const float v = buf.getSample (ch,i);
            out.setSample (ch, start+i, v);
            if (! std::isfinite (v)) sawNaN = true;
            peak = std::max (peak, (double) std::fabs (v));
            if (start + i >= settle)
                maxDryErr = std::max (maxDryErr, (double) std::fabs (v - in.getSample (ch,i)));
        }
    }
    return out;
}

// Peak |sample| of channel 0 within [from,to).
double peakWindow (const juce::AudioBuffer<float>& b, int from, int to)
{
    double m = 0.0;
    for (int i = std::max (0, from); i < to && i < b.getNumSamples(); ++i)
        m = std::max (m, (double) std::fabs (b.getSample (0, i)));
    return m;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    int fails = 0;
    double dryErr = 0, peak = 0; bool nan = false;

    // 1) mix=0 -> exact dry pass-through at unity output.
    render ([] (auto& a){ sp (a,"mix",0.0f); sp (a,"output",0.0f); sp (a,"feedback",0.5f); },
            0.4, dryErr, peak, nan, false);
    std::printf ("dry-pass err=%.1e  peak=%.3f  nan=%d\n", dryErr, peak, (int) nan);
    if (dryErr > 5.0e-4) { std::printf ("  FAIL not dry pass-through at mix=0\n"); ++fails; }
    if (nan)             { std::printf ("  FAIL NaN in dry-pass\n"); ++fails; }

    // 2) Impulse -> spaced, decaying repeats. 1/8 at 120 BPM = 0.25 s = 12000 smp.
    //    Wet-only (mix=1) so the repeats are isolated from the dry impulse.
    auto rep = render ([] (auto& a){
                          sp (a,"sync",1.0f); sp (a,"div",3.0f);  // 1/8
                          sp (a,"mix",1.0f); sp (a,"feedback",0.6f);
                          sp (a,"tone",0.0f); sp (a,"diffusion",0.0f);
                          sp (a,"duck",0.0f); sp (a,"pingpong",0.0f); sp (a,"output",0.0f);
                       }, 1.0, dryErr, peak, nan, true);
    const double D = 12000.0;
    const double r1 = peakWindow (rep, (int)(D - 400), (int)(D + 400));       // 1st repeat
    const double r2 = peakWindow (rep, (int)(2*D - 400), (int)(2*D + 400));   // 2nd repeat
    const double gap = peakWindow (rep, (int)(D + 2000), (int)(2*D - 2000));  // between them
    std::printf ("repeat1=%.4f  repeat2=%.4f  gap=%.4f  nan=%d\n", r1, r2, gap, (int) nan);
    if (r1 < 1.0e-2)          { std::printf ("  FAIL no first repeat at 1/8 time\n"); ++fails; }
    if (r1 <= gap * 3.0)      { std::printf ("  FAIL first repeat not above the gap\n"); ++fails; }
    if (r2 >= r1)             { std::printf ("  FAIL repeats not decaying (fb<1)\n"); ++fails; }
    if (r2 < 1.0e-3)          { std::printf ("  FAIL second repeat vanished (no feedback)\n"); ++fails; }
    if (nan)                  { std::printf ("  FAIL NaN in repeat render\n"); ++fails; }

    // 3) Feedback near max stays bounded, even with tone + diffusion engaged, over
    //    a long tail. A sustained tone drives the loop hard; output must not blow up.
    render ([] (auto& a){
               sp (a,"sync",1.0f); sp (a,"div",6.0f);   // 1/16, fast recirculation
               sp (a,"mix",1.0f); sp (a,"feedback",0.95f);
               sp (a,"tone",0.6f); sp (a,"diffusion",0.8f);
               sp (a,"width",1.0f); sp (a,"pingpong",1.0f); sp (a,"output",0.0f);
            }, 6.0, dryErr, peak, nan, false);
    std::printf ("max-feedback peak=%.3f  nan=%d\n", peak, (int) nan);
    if (! (peak < 8.0)) { std::printf ("  FAIL runaway at near-max feedback\n"); ++fails; }
    if (nan)            { std::printf ("  FAIL NaN at near-max feedback\n"); ++fails; }

    std::printf (fails == 0 ? "Echo contract ok\n" : "Echo contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
