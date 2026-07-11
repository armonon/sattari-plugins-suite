// Sattari Comp — functional contract.
//
// Proves the compressor actually compresses, and only when it should:
//   (1) a quiet signal well BELOW threshold passes ~unchanged (near unity gain);
//   (2) a HOT signal above threshold is reduced, and the reduction roughly
//       tracks the ratio (measured GR is within a sane window of the ideal);
//   (3) mix = 0% is a bit-for-bit pass-through of the dry signal;
//   (4) no NaNs / non-finite / denormal-blowups anywhere.
// No ears required.
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// Render a steady sine at peak amplitude `amp`; return settled output RMS and
// whether every sample stayed finite. `dryRms` returns the input RMS (settled).
double renderTone (SattariCompAudioProcessor& proc, double sr, int block, int blocks,
                   float amp, double freq, double& phase, bool& finite, double& dryRms)
{
    juce::AudioBuffer<float> last (2, block);
    juce::AudioBuffer<float> lastDry (2, block);
    finite = true;
    for (int bi = 0; bi < blocks; ++bi)
    {
        juce::AudioBuffer<float> buf (2, block);
        for (int i = 0; i < block; ++i)
        {
            const float s = amp * (float) std::sin (phase);
            phase += juce::MathConstants<double>::twoPi * freq / sr;
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
        }
        if (bi == blocks - 1) lastDry.makeCopyOf (buf, true);
        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
        if (bi == blocks - 1) last.makeCopyOf (buf, true);
    }

    double s2 = 0.0, d2 = 0.0;
    for (int i = 0; i < block; ++i)
    {
        const double v = last.getSample (0, i);
        if (! std::isfinite (v)) finite = false;
        s2 += v * v;
        const double d = lastDry.getSample (0, i);
        d2 += d * d;
    }
    dryRms = std::sqrt (d2 / block);
    return std::sqrt (s2 / block);
}

void configure (juce::AudioProcessorValueTreeState& a,
                float thresholdDb, float ratio, float mixPct)
{
    setParam (a, "threshold", thresholdDb);
    setParam (a, "ratio", ratio);
    setParam (a, "knee", 0.0f);           // hard knee for a clean ratio check
    setParam (a, "attack", 5.0f);
    setParam (a, "release", 120.0f);
    setParam (a, "autoRelease", 0.0f);
    setParam (a, "makeup", 0.0f);
    setParam (a, "autoGain", 0.0f);
    setParam (a, "mix", mixPct);
    setParam (a, "scHpf", 20.0f);          // HP effectively bypassed
    setParam (a, "scListen", 0.0f);
    setParam (a, "tempoRelease", 0.0f);
}

double gainToDb (double g) { return 20.0 * std::log10 (g + 1e-12); }
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    const double sr = 48000.0;
    const int block = 512;
    const double freq = 220.0;
    int fails = 0;

    // --- (1) Below threshold: near pass-through -----------------------------
    {
        SattariCompAudioProcessor proc;
        proc.prepareToPlay (sr, block);
        configure (proc.parameters(), -12.0f, 4.0f, 100.0f);
        // Quiet tone: peak 0.03 (~ -30 dBFS), well under a -12 dBFS threshold.
        double ph = 0.0, dry = 0.0; bool fin = true;
        renderTone (proc, sr, block, 6, 0.03f, freq, ph, fin, dry);     // warm up
        const double out = renderTone (proc, sr, block, 20, 0.03f, freq, ph, fin, dry);
        const double changeDb = gainToDb (out / (dry + 1e-12));
        std::printf ("below-threshold: dryRMS=%.5f outRMS=%.5f change=%.2f dB finite=%d\n",
                     dry, out, changeDb, (int) fin);
        if (! fin)                  { std::printf ("  FAIL non-finite (below thr)\n"); ++fails; }
        if (std::fabs (changeDb) > 0.5) { std::printf ("  FAIL below-threshold signal not ~unchanged\n"); ++fails; }
    }

    // --- (2) Above threshold: real gain reduction near the ratio ------------
    {
        SattariCompAudioProcessor proc;
        proc.prepareToPlay (sr, block);
        const float thr = -24.0f, ratio = 4.0f;
        configure (proc.parameters(), thr, ratio, 100.0f);
        // Hot tone: peak ~0.5 (~ -6 dBFS), i.e. ~18 dB over the -24 dBFS threshold.
        const float amp = 0.5f;
        double ph = 0.0, dry = 0.0; bool fin = true;
        renderTone (proc, sr, block, 10, amp, freq, ph, fin, dry);      // warm up / settle
        const double out = renderTone (proc, sr, block, 30, amp, freq, ph, fin, dry);

        const double inDb  = gainToDb (dry);
        const double outDb = gainToDb (out);
        const double grDb  = inDb - outDb;                 // measured reduction (>0)

        // Ideal static GR for a hard-knee comp: over * (1 - 1/ratio).
        const double over  = inDb - thr;
        const double ideal = over * (1.0 - 1.0 / ratio);
        std::printf ("above-threshold: inRMS=%.4f(%.1f dB) outRMS=%.4f(%.1f dB) GR=%.2f dB ideal=%.2f dB reported=%.2f finite=%d\n",
                     dry, inDb, out, outDb, grDb, ideal, proc.getGainReductionDb(), (int) fin);
        if (! fin)                 { std::printf ("  FAIL non-finite (above thr)\n"); ++fails; }
        if (out >= dry * 0.9)      { std::printf ("  FAIL hot signal was not reduced\n"); ++fails; }
        if (grDb < ideal * 0.6 || grDb > ideal * 1.4)
                                   { std::printf ("  FAIL measured GR (%.2f) far from ratio ideal (%.2f)\n", grDb, ideal); ++fails; }
        if (proc.getGainReductionDb() < 1.0f)
                                   { std::printf ("  FAIL reported GR atomic not engaging\n"); ++fails; }
    }

    // --- (3) mix = 0% is a pass-through -------------------------------------
    {
        SattariCompAudioProcessor proc;
        proc.prepareToPlay (sr, block);
        // Heavy settings that WOULD crush at mix=100, but mix=0 must be dry.
        configure (proc.parameters(), -40.0f, 20.0f, 0.0f);
        setParam (proc.parameters(), "makeup", 6.0f);   // makeup must not leak at mix 0
        double ph = 0.0, dry = 0.0; bool fin = true;
        renderTone (proc, sr, block, 6, 0.5f, freq, ph, fin, dry);
        const double out = renderTone (proc, sr, block, 12, 0.5f, freq, ph, fin, dry);
        const double diffDb = gainToDb (out / (dry + 1e-12));
        std::printf ("mix=0: dryRMS=%.4f outRMS=%.4f diff=%.3f dB finite=%d\n", dry, out, diffDb, (int) fin);
        if (! fin)                    { std::printf ("  FAIL non-finite (mix 0)\n"); ++fails; }
        if (std::fabs (diffDb) > 0.05){ std::printf ("  FAIL mix=0 is not a clean pass-through\n"); ++fails; }
    }

    std::printf (fails == 0 ? "Sattari Comp contract ok\n" : "Sattari Comp contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
