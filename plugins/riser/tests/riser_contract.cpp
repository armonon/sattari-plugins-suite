// Sattari Riser — functional contract.
//  Proves: (1) not armed => exact pass-through of the input; (2) armed => a
//  rising build whose energy climbs from the start of the build toward the
//  landing; (3) output stays finite; (4) output stays bounded (soft ceiling).
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// Fill a buffer block with a steady 220 Hz tone at 0.25 amplitude.
void fillTone (juce::AudioBuffer<float>& buf, double& ph, double sr)
{
    const int n = buf.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        const float s = 0.25f * (float) std::sin (ph);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            buf.setSample (ch, i, s);
        ph += 2.0 * M_PI * 220.0 / sr;
        if (ph > 2.0 * M_PI) ph -= 2.0 * M_PI;
    }
}
} // namespace

int main()
{
    const double sr = 48000.0;
    const int block = 256;
    int fails = 0;

    // ---- (1) NOT ARMED => exact pass-through --------------------------------
    {
        SattariRiserAudioProcessor p; auto& a = p.parameters();
        setParam (a, "arm", 0.0f);
        setParam (a, "hostTrigger", 0.0f);
        setParam (a, "mix", 0.8f);
        setParam (a, "output", 0.0f);
        p.prepareToPlay (sr, block);

        double ph = 0.0; juce::MidiBuffer midi;
        double maxDiff = 0.0; bool finite = true;
        for (int blk = 0; blk < 40; ++blk)
        {
            juce::AudioBuffer<float> buf (2, block);
            fillTone (buf, ph, sr);
            juce::AudioBuffer<float> ref (buf);         // copy the input
            p.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    if (! std::isfinite (v)) finite = false;
                    maxDiff = std::max (maxDiff, (double) std::fabs (v - ref.getSample (ch, i)));
                }
        }
        std::printf ("pass-through  maxDiff=%.2e finite=%d\n", maxDiff, (int) finite);
        if (! finite)       { std::printf ("  FAIL non-finite in pass-through\n"); ++fails; }
        if (maxDiff > 1e-6) { std::printf ("  FAIL not-armed is not a pass-through\n"); ++fails; }
    }

    // ---- (2)(3)(4) ARMED => rising build, bounded, finite -------------------
    {
        SattariRiserAudioProcessor p; auto& a = p.parameters();
        setParam (a, "mode", 0.0f);         // Riser
        setParam (a, "length", 1.0f);       // 1 bar (index 1)
        setParam (a, "hostTrigger", 0.0f);
        setParam (a, "noiseTone", 0.7f);
        setParam (a, "pitchRise", 12.0f);
        setParam (a, "stutter", 0.4f);
        setParam (a, "mix", 1.0f);          // fully wet so the build dominates
        setParam (a, "output", 0.0f);
        p.prepareToPlay (sr, block);

        // 1 bar at 120 BPM = 4 beats * 0.5 s = 2.0 s of build. Render ~3 s.
        const double buildSeconds = 2.0;
        const int totalN = (int) (sr * 3.0);
        juce::AudioBuffer<float> out (2, totalN);
        double ph = 0.0; juce::MidiBuffer midi; bool finite = true; double peak = 0.0;

        int rendered = 0;
        bool armed = false;
        for (int start = 0; start < totalN; start += block)
        {
            const int len = std::min (block, totalN - start);
            juce::AudioBuffer<float> buf (2, len);
            fillTone (buf, ph, sr);

            // Arm on the second block (a rising edge from the default 0).
            if (! armed && start >= block)
            {
                setParam (a, "arm", 1.0f);
                armed = true;
            }

            p.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < len; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    if (! std::isfinite (v)) finite = false;
                    peak = std::max (peak, (double) std::fabs (v));
                    out.setSample (ch, start + i, v);
                }
            rendered += len;
        }
        juce::ignoreUnused (rendered);

        // Energy in the first quarter vs the last quarter of the 2 s build.
        auto rms = [&out] (int a0, int a1)
        {
            double s2 = 0.0; int c = 0;
            for (int g = a0; g < a1; ++g) { const float v = out.getSample (0, g); s2 += v * v; ++c; }
            return c ? std::sqrt (s2 / c) : 0.0;
        };
        const int b0 = block;                          // build starts ~2nd block
        const int qtr = (int) (sr * buildSeconds / 4.0);
        const double early = rms (b0, b0 + qtr);
        const double late  = rms (b0 + 3 * qtr, b0 + 4 * qtr);

        std::printf ("build  earlyRMS=%.4f lateRMS=%.4f peak=%.3f finite=%d\n",
                     early, late, peak, (int) finite);

        if (! finite)          { std::printf ("  FAIL non-finite during build\n"); ++fails; }
        if (peak > 1.2)        { std::printf ("  FAIL runaway output (peak > 1.2)\n"); ++fails; }
        if (peak < 0.05)       { std::printf ("  FAIL build produced no signal\n"); ++fails; }
        if (late <= early * 1.5) { std::printf ("  FAIL build energy did not clearly rise into the landing\n"); ++fails; }

        // (2b) It must RESOLVE: after the build window the plugin returns to
        // pass-through (isBuilding() false, energy near the dry tone level).
        if (p.isBuilding()) { std::printf ("  FAIL still building after the full length elapsed\n"); ++fails; }
    }

    std::printf (fails == 0 ? "Riser contract ok\n" : "Riser contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
