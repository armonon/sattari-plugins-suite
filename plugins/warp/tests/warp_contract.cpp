// Sattari Warp — functional DSP contract.
// Proves the transformer (1) passes a signal through finite + non-silent, and
// (2) DOSE actually changes the sound (the whole point of the plugin).
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

// Render a voiced-ish tone through the processor and return the output buffer.
juce::AudioBuffer<float> render (SattariWarpAudioProcessor& proc, double sr, int block, double seconds)
{
    proc.prepareToPlay (sr, block);
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> buf (2, n);
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float s = 0.4f * (float) std::sin (ph) + 0.15f * (float) std::sin (ph * 2.01);
        buf.setSample (0, i, s); buf.setSample (1, i, s);
        ph += 2.0 * M_PI * 150.0 / sr;
    }
    juce::MidiBuffer midi;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> sub (buf.getArrayOfWritePointers(), 2, start, len);
        proc.processBlock (sub, midi);
    }
    return buf;
}

double rms (const juce::AudioBuffer<float>& b)
{
    double s = 0.0; int c = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i) { const double x = b.getSample (ch, i); s += x * x; ++c; }
    return c ? std::sqrt (s / c) : 0.0;
}

double maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double m = 0.0;
    const int n = std::min (a.getNumSamples(), b.getNumSamples());
    for (int i = 0; i < n; ++i) m = std::max (m, (double) std::fabs (a.getSample (0, i) - b.getSample (0, i)));
    return m;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    const double sr = 48000.0; const int block = 512;
    int fails = 0;

    auto cfg = [&] (float dose, float stutter) {
        SattariWarpAudioProcessor p; auto& a = p.parameters();
        setParam (a, "vowel", 0.4f); setParam (a, "vowelAmt", 0.6f);
        setParam (a, "robotAmt", 0.4f); setParam (a, "robotFreq", 90.0f);
        setParam (a, "drive", 0.5f); setParam (a, "crush", 0.4f);
        setParam (a, "stutter", stutter); setParam (a, "mix", 1.0f);
        setParam (a, "dose", dose);
        return render (p, sr, block, 1.0);
    };

    const auto clean = cfg (0.0f, 0.0f);   // dose 0 -> ~dry
    const auto dosed = cfg (1.0f, 0.0f);   // full dose

    bool finite = true; float peak = 0.0f;
    for (int ch = 0; ch < dosed.getNumChannels(); ++ch)
        for (int i = 0; i < dosed.getNumSamples(); ++i)
        {
            const float v = dosed.getSample (ch, i);
            if (! std::isfinite (v)) finite = false;
            peak = std::max (peak, std::fabs (v));
        }

    const double rClean = rms (clean), rDosed = rms (dosed), diff = maxDiff (clean, dosed);
    std::printf ("dose0 rms=%.4f  dose1 rms=%.4f  peak=%.3f  |dose1-dose0|max=%.4f  finite=%d\n",
                 rClean, rDosed, peak, diff, (int) finite);

    if (! finite)          { std::printf ("  FAIL non-finite\n"); ++fails; }
    if (rDosed < 1.0e-3)   { std::printf ("  FAIL dosed output silent\n"); ++fails; }
    if (peak > 4.0f)       { std::printf ("  FAIL runaway peak\n"); ++fails; }
    if (diff < 0.02)       { std::printf ("  FAIL Dose does not change the sound\n"); ++fails; }

    // Stutter engages without blowing up.
    const auto st = cfg (1.0f, 0.9f);
    bool stFinite = true; for (int i = 0; i < st.getNumSamples(); ++i) if (! std::isfinite (st.getSample (0, i))) stFinite = false;
    if (! stFinite)        { std::printf ("  FAIL stutter non-finite\n"); ++fails; }

    std::printf (fails == 0 ? "Warp contract ok\n" : "Warp contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
