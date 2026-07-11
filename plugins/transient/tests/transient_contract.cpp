// Sattari Transient — functional contract.
// Feeds percussive bursts (sharp onset + exponential decay). Proves Attack
// raises/lowers the onset PEAK and Sustain raises/lowers the decay TAIL, while
// everything stays finite + bounded.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// Render bursts; return global peak and mid-decay tail RMS.
void render (float attack, float sustain, double& peak, double& tailRms, bool& finite)
{
    const double sr = 48000.0; const int block = 512;
    const int period = 12000;          // burst every 0.25 s
    SattariTransientAudioProcessor p; auto& a = p.parameters();
    setParam (a, "attack", attack); setParam (a, "sustain", sustain); setParam (a, "output", 0.0f);
    p.prepareToPlay (sr, block);

    const int n = period * 12;
    juce::AudioBuffer<float> out (2, n);
    double ph = 0.0; juce::MidiBuffer midi; finite = true;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len);
        for (int i = 0; i < len; ++i)
        {
            const int g = start + i;
            const int since = g % period;
            const float envv = std::exp (-(float) since / (0.05f * (float) sr));   // 50 ms decay
            const float s = 0.7f * envv * (float) std::sin (ph);
            buf.setSample (0, i, s); buf.setSample (1, i, s);
            ph += 2.0 * M_PI * 200.0 / sr;
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < len; ++i) out.setSample (ch, start + i, buf.getSample (ch, i));
    }

    peak = 0.0; double s2 = 0.0; int cnt = 0;
    for (int g = period * 2; g < n; ++g)     // skip the first bursts (detectors settling)
    {
        const float v = out.getSample (0, g);
        if (! std::isfinite (v)) finite = false;
        peak = std::max (peak, (double) std::fabs (v));
        const int since = g % period;
        if (since >= (int) (0.06 * sr) && since < (int) (0.14 * sr)) { s2 += v * v; ++cnt; }  // tail window
    }
    tailRms = cnt ? std::sqrt (s2 / cnt) : 0.0;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    int fails = 0;
    double pN=0,tN=0, pA=0,tA=0, pC=0,tC=0, pS=0,tS=0, pD=0,tD=0;
    bool f1=true,f2=true,f3=true,f4=true,f5=true;
    render ( 0.0f,  0.0f, pN, tN, f1);
    render ( 0.9f,  0.0f, pA, tA, f2);   // attack boost
    render (-0.9f,  0.0f, pC, tC, f3);   // attack cut
    render ( 0.0f,  0.9f, pS, tS, f4);   // sustain boost
    render ( 0.0f, -0.9f, pD, tD, f5);   // sustain cut

    std::printf ("peak  neutral=%.3f atk+=%.3f atk-=%.3f\n", pN, pA, pC);
    std::printf ("tail  neutral=%.4f sus+=%.4f sus-=%.4f\n", tN, tS, tD);

    if (! (f1&&f2&&f3&&f4&&f5)) { std::printf ("  FAIL non-finite\n"); ++fails; }
    if (pA <= pN * 1.05)        { std::printf ("  FAIL attack+ did not boost the onset peak\n"); ++fails; }
    if (pC >= pN * 0.95)        { std::printf ("  FAIL attack- did not soften the onset peak\n"); ++fails; }
    if (tS <= tD * 1.1)         { std::printf ("  FAIL sustain+ not clearly louder tail than sustain-\n"); ++fails; }
    if (pA > 4.0)               { std::printf ("  FAIL runaway peak\n"); ++fails; }

    std::printf (fails == 0 ? "Transient contract ok\n" : "Transient contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
