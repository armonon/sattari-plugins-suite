// Sattari De-Ess — functional contract.
// Feeds a low tone (300 Hz) + a high "sibilant" tone (7 kHz). Proves that with a
// low threshold the 7 kHz energy is strongly reduced while the 300 Hz tone is
// preserved (Split mode), and that a 0 dB threshold passes it through untouched.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// Single-bin amplitude estimate (Goertzel-style correlation) over ch0.
double magnitudeAt (const juce::AudioBuffer<float>& b, double sr, double f)
{
    const int n = b.getNumSamples(); const int from = n / 3;
    const float* d = b.getReadPointer (0);
    double sSin = 0.0, sCos = 0.0;
    for (int i = from; i < n; ++i)
    {
        const double t = (double) i / sr;
        sSin += d[i] * std::sin (2.0 * M_PI * f * t);
        sCos += d[i] * std::cos (2.0 * M_PI * f * t);
    }
    const int cnt = n - from;
    return 2.0 * std::sqrt (sSin * sSin + sCos * sCos) / cnt;
}

juce::AudioBuffer<float> render (float thresholdDb, double& lowAmp, double& hiAmp, bool& finite)
{
    const double sr = 48000.0; const int block = 512;
    SattariDeEssAudioProcessor p; auto& a = p.parameters();
    setParam (a, "freq", 4500.0f);
    setParam (a, "threshold", thresholdDb);
    setParam (a, "range", 18.0f);
    setParam (a, "mode", 0.0f);       // Split
    setParam (a, "listen", 0.0f);
    setParam (a, "output", 0.0f);
    p.prepareToPlay (sr, block);

    const int n = (int) (sr * 1.0);
    juce::AudioBuffer<float> out (2, n);
    double phL = 0.0, phH = 0.0; juce::MidiBuffer midi; finite = true;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len);
        for (int i = 0; i < len; ++i)
        {
            const float s = 0.3f * (float) std::sin (phL) + 0.3f * (float) std::sin (phH);
            buf.setSample (0, i, s); buf.setSample (1, i, s);
            phL += 2.0 * M_PI * 300.0 / sr;
            phH += 2.0 * M_PI * 7000.0 / sr;
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < len; ++i)
            {
                const float v = buf.getSample (ch, i);
                if (! std::isfinite (v)) finite = false;
                out.setSample (ch, start + i, v);
            }
    }
    lowAmp = magnitudeAt (out, sr, 300.0);
    hiAmp  = magnitudeAt (out, sr, 7000.0);
    return out;
}
} // namespace

int main()
{
    int fails = 0;
    bool f1 = true, f2 = true;
    double lowOff = 0, hiOff = 0, lowOn = 0, hiOn = 0;
    render (0.0f,   lowOff, hiOff, f1);   // threshold 0 dB -> no de-essing
    render (-45.0f, lowOn,  hiOn,  f2);   // low threshold -> heavy de-essing

    std::printf ("bypass: low=%.4f hi=%.4f   deess: low=%.4f hi=%.4f\n",
                 lowOff, hiOff, lowOn, hiOn);

    if (! (f1 && f2))          { std::printf ("  FAIL non-finite\n"); ++fails; }
    if (hiOn > hiOff * 0.5)    { std::printf ("  FAIL sibilant band not reduced (>= half remains)\n"); ++fails; }
    if (lowOn < lowOff * 0.85) { std::printf ("  FAIL low tone was harmed by de-essing\n"); ++fails; }

    std::printf (fails == 0 ? "De-Ess contract ok\n" : "De-Ess contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
