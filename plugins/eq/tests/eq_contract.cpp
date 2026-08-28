// Sattari EQ — functional contract.
// Proves a bell boost/cut changes that band's magnitude, the HPF removes lows,
// and a dynamic band engages MORE when the band is loud than when it is quiet.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <functional>

namespace
{
void sp (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{ if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); }

double magAt (const juce::AudioBuffer<float>& b, double sr, double f)
{
    const int n = b.getNumSamples(); const int from = n / 2;
    const float* d = b.getReadPointer (0);
    double sSin = 0.0, sCos = 0.0;
    for (int i = from; i < n; ++i)
    { const double t = (double) i / sr; sSin += d[i]*std::sin(2*M_PI*f*t); sCos += d[i]*std::cos(2*M_PI*f*t); }
    return 2.0 * std::sqrt (sSin*sSin + sCos*sCos) / (n - from);
}

// Feed a `freq` sine at `amp` through the EQ configured by `setup`; return mag at freq.
double run (double freq, float amp, std::function<void(juce::AudioProcessorValueTreeState&)> setup)
{
    const double sr = 48000.0; const int block = 512;
    SattariEQAudioProcessor p; auto& a = p.parameters();
    setup (a);
    p.prepareToPlay (sr, block);
    const int n = (int) (sr * 1.0);
    juce::AudioBuffer<float> out (2, n);
    double ph = 0.0; juce::MidiBuffer midi;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len);
        for (int i = 0; i < len; ++i)
        { const float s = amp * (float) std::sin (ph); buf.setSample (0,i,s); buf.setSample (1,i,s); ph += 2.0*M_PI*freq/sr; }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < len; ++i) out.setSample (ch, start+i, buf.getSample (ch,i));
    }
    return magAt (out, sr, freq);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    int fails = 0;
    auto flat = [] (juce::AudioProcessorValueTreeState&) {};

    const double m0    = run (1000.0, 0.3f, flat);
    const double mBoost = run (1000.0, 0.3f, [] (auto& a){ sp (a,"f2",1000.0f); sp (a,"g2",12.0f); sp (a,"q2",1.0f); });
    const double mCut   = run (1000.0, 0.3f, [] (auto& a){ sp (a,"f2",1000.0f); sp (a,"g2",-12.0f); sp (a,"q2",1.0f); });
    std::printf ("1kHz: flat=%.4f boost=%.4f cut=%.4f\n", m0, mBoost, mCut);
    if (mBoost < m0 * 2.5) { std::printf ("  FAIL +12 dB bell did not boost enough\n"); ++fails; }
    if (mCut > m0 * 0.55)  { std::printf ("  FAIL -12 dB bell did not cut enough\n"); ++fails; }

    // HPF removes lows: 80 Hz with HPF at 300 vs off.
    const double lowOff = run (80.0, 0.3f, flat);
    const double lowHP  = run (80.0, 0.3f, [] (auto& a){ sp (a,"hpf",300.0f); });
    std::printf ("80Hz: hpfOff=%.4f hpfOn=%.4f\n", lowOff, lowHP);
    if (lowHP > lowOff * 0.5) { std::printf ("  FAIL HPF did not remove lows\n"); ++fails; }

    // Dynamic band: -12 dB cut with Dyn=1 engages more when loud than when quiet.
    auto dynCfg = [] (auto& a){ sp (a,"f2",1000.0f); sp (a,"g2",-12.0f); sp (a,"q2",1.0f); sp (a,"d2",1.0f); };
    const double loud  = run (1000.0, 0.8f,  dynCfg) / 0.8;    // out/in ratio, loud
    const double quiet = run (1000.0, 0.02f, dynCfg) / 0.02;   // out/in ratio, quiet
    std::printf ("dyn cut ratio: loud=%.3f quiet=%.3f\n", loud, quiet);
    if (loud > quiet * 0.75) { std::printf ("  FAIL dynamic band did not engage more when loud\n"); ++fails; }

    std::printf (fails == 0 ? "EQ contract ok\n" : "EQ contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
