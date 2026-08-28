// Sattari Shimmer — functional contract.
// Proves the pitch-shifted shimmer reverb behaves:
//   (1) mix=0 is a near-bit-exact dry pass-through;
//   (2) with mix>0 and a short input burst, a reverb tail persists AFTER the
//       input stops (energy after silence), i.e. the tank + shimmer bloom;
//   (3) even at extreme shimmer + size the feedback stays BOUNDED and DECAYS
//       (late tail below the peak tail -> no runaway), and everything is finite;
//   (4) no NaNs / infs anywhere;
//   (5) Key Snap quantizes the shimmer interval onto in-key scale tones.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <functional>

namespace
{
void sp (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{ if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); }

// Render `seconds` of audio. A 50 ms tone burst then silence (unless continuous),
// so the reverb/shimmer tail is whatever survives after the input stops.
// maxDryErr is measured after 0.2 s (past juce::Reverb's parameter ramp) against
// the raw dry input. `allFinite` is cleared if any sample is non-finite.
juce::AudioBuffer<float> render (std::function<void(juce::AudioProcessorValueTreeState&)> setup,
                                 double seconds, double& maxDryErr, bool& allFinite,
                                 bool continuous = false)
{
    const double sr = 48000.0; const int block = 512;
    SattariShimmerAudioProcessor p; auto& a = p.parameters();
    setup (a);
    p.prepareToPlay (sr, block);
    const int n = (int) (sr * seconds);
    const int settle = (int) (sr * 0.2);
    const int burst  = (int) (sr * 0.05);
    juce::AudioBuffer<float> out (2, n);
    double ph = 0.0; juce::MidiBuffer midi; maxDryErr = 0.0; allFinite = true;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len), in (2, len);
        for (int i = 0; i < len; ++i)
        {
            const int gi = start + i;
            const float s = continuous ? 0.35f * (float) std::sin (ph)
                                       : (gi < burst ? 0.5f * (float) std::sin (ph) : 0.0f);
            buf.setSample (0,i,s); buf.setSample (1,i,s);
            in.setSample (0,i,s);  in.setSample (1,i,s);
            ph += 2.0 * M_PI * 220.0 / sr;
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < len; ++i)
        {
            const float v = buf.getSample (ch,i);
            if (! std::isfinite (v)) allFinite = false;
            out.setSample (ch, start+i, v);
            if (start + i >= settle)
                maxDryErr = std::max (maxDryErr, (double) std::fabs (v - in.getSample (ch,i)));
        }
    }
    return out;
}

double rmsWindow (const juce::AudioBuffer<float>& b, int from, int to)
{
    double s = 0.0; int c = 0;
    for (int i = from; i < to && i < b.getNumSamples(); ++i)
    { const double v = b.getSample (0,i); s += v*v; ++c; }
    return c ? std::sqrt (s / c) : 0.0;
}
double peakAbs (const juce::AudioBuffer<float>& b)
{
    double m = 0.0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            m = std::max (m, (double) std::fabs (b.getSample (ch,i)));
    return m;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    int fails = 0;
    double e = 0; bool fin = true;

    // 1) mix=0 -> dry pass-through (continuous tone, measured after settle).
    render ([] (auto& a){ sp (a,"mix",0.0f); sp (a,"shimmer",0.5f); sp (a,"size",0.8f); },
            0.6, e, fin, true);
    std::printf ("dry-pass err=%.2e finite=%d\n", e, (int) fin);
    if (! fin)        { std::printf ("  FAIL non-finite at mix=0\n"); ++fails; }
    if (e > 5.0e-3)   { std::printf ("  FAIL not dry pass-through at mix=0\n"); ++fails; }

    // 2) mix>0 + a burst -> a tail persists AFTER the input stops, and the
    //    pitch-shifted feedback makes it BLOOM (energy still present a beat later,
    //    not just an instant reflection). Burst ends at 0.05 s.
    auto tailBuf = render ([] (auto& a)
        { sp (a,"mix",0.6f); sp (a,"shimmer",0.7f); sp (a,"size",0.8f);
          sp (a,"damp",0.3f); sp (a,"predelay",20.0f); },
        1.6, e, fin);
    const double tail    = rmsWindow (tailBuf, 28800, 57600);   // 0.6-1.2 s
    const double tailLate = rmsWindow (tailBuf, 43200, 72000);  // 0.9-1.5 s (still ringing)
    std::printf ("shimmer tail rms=%.5f late=%.5f finite=%d\n", tail, tailLate, (int) fin);
    if (! fin)             { std::printf ("  FAIL non-finite in tail render\n"); ++fails; }
    if (tail < 1.0e-3)     { std::printf ("  FAIL no reverb/shimmer tail after input stops\n"); ++fails; }
    if (tailLate < 1.0e-4) { std::printf ("  FAIL tail died immediately (no shimmer bloom)\n"); ++fails; }

    // 3) Extreme shimmer + size must stay BOUNDED and DECAY (no runaway feedback).
    //    Drive at max shimmer / max size for 18 s; after the initial upward bloom
    //    the dissipative loop must wind down toward silence, finite throughout.
    auto hot = render ([] (auto& a)
        { sp (a,"mix",1.0f); sp (a,"shimmer",0.95f); sp (a,"size",1.0f);
          sp (a,"damp",0.0f); sp (a,"predelay",0.0f); },
        18.0, e, fin);
    const double bloomPk = rmsWindow (hot, (int)(48000*0.5), (int)(48000*5.0));   // early bloom region
    const double late    = rmsWindow (hot, (int)(48000*15.0), (int)(48000*17.5)); // long after input
    const double pk      = peakAbs (hot);
    std::printf ("hot loop  bloomPeak=%.5f late=%.5f peak=%.3f finite=%d\n", bloomPk, late, pk, (int) fin);
    if (! fin)              { std::printf ("  FAIL non-finite under hot feedback (NaN/inf)\n"); ++fails; }
    if (pk > 4.0)           { std::printf ("  FAIL runaway peak (blowup)\n"); ++fails; }
    // The tank stops receiving input at 0.05 s. The loop is dissipative: long
    // after the input it must have wound DOWN both relative to its bloom and to
    // near-silence in absolute terms — not settle into a sustained limit cycle.
    if (late > bloomPk * 0.6) { std::printf ("  FAIL feedback not decaying vs bloom (sustaining)\n"); ++fails; }
    if (late > 1.0e-3)        { std::printf ("  FAIL feedback has not decayed to near-silence (runaway)\n"); ++fails; }

    // 4) NaN sweep already covered by the `fin` flags above.

    // 5) Key Snap: quantize the +12 interval into C major -> still an in-key,
    //    upward interval; a fifth (+7) already in C major stays +7.
    {
        SattariShimmerAudioProcessor p;
        using sattari::dsp::ScaleId;
        const int oct  = p.effectiveShimmerSemis (12, 0, ScaleId::Major);   // C+12 = C, in-key
        const int fifth = p.effectiveShimmerSemis (7, 0, ScaleId::Major);   // C+7 = G, in C major
        // A raised fourth into C major: +6 (F#) is out of key -> should snap to
        // an in-key tone (F=+5 or G=+7), not stay at +6.
        const int tritone = p.effectiveShimmerSemis (6, 0, ScaleId::Major);
        std::printf ("keysnap oct=%d fifth=%d tritone->%d\n", oct, fifth, tritone);
        auto inCMajor = [] (int semis) { const int pc = ((semis % 12) + 12) % 12;
            return pc==0||pc==2||pc==4||pc==5||pc==7||pc==9||pc==11; };
        if (! inCMajor (oct))      { std::printf ("  FAIL octave not in C major\n"); ++fails; }
        if (fifth != 7)            { std::printf ("  FAIL fifth in-key should stay +7\n"); ++fails; }
        if (! inCMajor (tritone))  { std::printf ("  FAIL tritone did not snap into key\n"); ++fails; }
        if (tritone == 6)          { std::printf ("  FAIL tritone left out of key\n"); ++fails; }
        if (oct < 3 || fifth < 3 || tritone < 3)
                                   { std::printf ("  FAIL snapped interval collapsed below a min 3rd\n"); ++fails; }
    }

    std::printf (fails == 0 ? "Shimmer contract ok\n" : "Shimmer contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
