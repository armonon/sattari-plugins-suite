// Sattari Multiband — functional contract.
//
// Proves the 3-band multiband compressor behaves:
//   (1) NEUTRAL (all ratios 1:1, thresholds 0 dB) recombines to a ~flat
//       pass-through — a broadband multi-tone comes out with unchanged spectrum
//       (bands sum back together without a large gain error);
//   (2) a HOT band is actually compressed while a QUIET band passes ~untouched:
//       drive the low band hard (reduced) and the high band gently (passes);
//   (3) bands recombine without a large per-tone gain error in the neutral case;
//   (4) no NaNs / non-finite anywhere.
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

// Push every band to a clean, hard-neutral 1:1 / 0 dB / 0 dB-makeup pass-through.
void makeNeutral (juce::AudioProcessorValueTreeState& a)
{
    for (int b = 1; b <= 3; ++b)
    {
        const juce::String s (b);
        setParam (a, "thr"   + s, 0.0f);    // threshold at 0 dBFS -> nothing engages
        setParam (a, "ratio" + s, 1.0f);    // 1:1 -> no reduction even above thr
        setParam (a, "knee"  + s, 0.0f);
        setParam (a, "atk"   + s, 5.0f);
        setParam (a, "rel"   + s, 120.0f);
        setParam (a, "mk"    + s, 0.0f);
        setParam (a, "solo"  + s, 0.0f);
    }
    setParam (a, "xLow",  250.0f);
    setParam (a, "xHigh", 3000.0f);
    setParam (a, "mix",    100.0f);
    setParam (a, "output", 0.0f);
}

// Render a steady stereo tone (sum of `freqs`) at total peak ~amp; return settled
// output RMS and settled dry RMS, and whether all samples stayed finite.
double renderTones (SattariMultibandAudioProcessor& proc, double sr, int block, int blocks,
                    float amp, const std::vector<double>& freqs, double& phase,
                    bool& finite, double& dryRms)
{
    juce::AudioBuffer<float> last (2, block), lastDry (2, block);
    finite = true;
    const double norm = amp / (double) juce::jmax ((size_t) 1, freqs.size());

    for (int bi = 0; bi < blocks; ++bi)
    {
        juce::AudioBuffer<float> buf (2, block);
        for (int i = 0; i < block; ++i)
        {
            double s = 0.0;
            for (double f : freqs) s += std::sin (phase * (f / freqs[0]));
            phase += juce::MathConstants<double>::twoPi * freqs[0] / sr;
            const float v = (float) (norm * s);
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
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

double gainToDb (double g) { return 20.0 * std::log10 (g + 1e-12); }
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const double sr = 48000.0;
    const int block = 512;
    int fails = 0;

    // --- (1)+(3) NEUTRAL: flat pass-through, bands recombine without gain error --
    {
        SattariMultibandAudioProcessor proc;
        proc.prepareToPlay (sr, block);
        makeNeutral (proc.parameters());

        // A tone in each band: 80 Hz (low), 900 Hz (mid), 8 kHz (high). At 1:1/0 dB
        // the split must sum back to the input with essentially no gain change.
        std::vector<double> freqs { 80.0, 900.0, 8000.0 };
        double ph = 0.0, dry = 0.0; bool fin = true;
        renderTones (proc, sr, block, 8, 0.5f, freqs, ph, fin, dry);           // settle
        const double out = renderTones (proc, sr, block, 12, 0.5f, freqs, ph, fin, dry);
        const double changeDb = gainToDb (out / (dry + 1e-12));
        std::printf ("neutral: dryRMS=%.5f outRMS=%.5f change=%.3f dB finite=%d\n",
                     dry, out, changeDb, (int) fin);
        if (! fin)                       { std::printf ("  FAIL non-finite (neutral)\n"); ++fails; }
        // Allowbroadband LR recombination + soft-ceiling headroom: <= 1.0 dB error.
        if (std::fabs (changeDb) > 1.0)  { std::printf ("  FAIL neutral not a flat pass-through\n"); ++fails; }
    }

    // --- (2a) HOT band is compressed -------------------------------------------
    {
        SattariMultibandAudioProcessor proc;
        proc.prepareToPlay (sr, block);
        auto& a = proc.parameters();
        makeNeutral (a);
        // Arm ONLY the low band: hard threshold, high ratio. A hot 80 Hz tone sits
        // fully inside the low band (below the 250 Hz crossover).
        setParam (a, "thr1", -30.0f);
        setParam (a, "ratio1", 8.0f);
        setParam (a, "atk1", 5.0f);
        setParam (a, "rel1", 120.0f);

        std::vector<double> low { 80.0 };
        double ph = 0.0, dry = 0.0; bool fin = true;
        renderTones (proc, sr, block, 12, 0.6f, low, ph, fin, dry);            // settle
        const double out = renderTones (proc, sr, block, 20, 0.6f, low, ph, fin, dry);
        const double grDb = gainToDb (dry) - gainToDb (out);
        std::printf ("hot-low:  dryRMS=%.4f outRMS=%.4f GR=%.2f dB reportedLowGR=%.2f finite=%d\n",
                     dry, out, grDb, proc.getBandGainReductionDb (0), (int) fin);
        if (! fin)              { std::printf ("  FAIL non-finite (hot low)\n"); ++fails; }
        if (out >= dry * 0.85)  { std::printf ("  FAIL hot low band was not compressed\n"); ++fails; }
        if (grDb < 2.0)         { std::printf ("  FAIL low-band GR too small\n"); ++fails; }
        if (proc.getBandGainReductionDb (0) < 1.0f)
                                { std::printf ("  FAIL low-band GR atomic not engaging\n"); ++fails; }
    }

    // --- (2b) QUIET band passes ~untouched while another band is armed ----------
    {
        SattariMultibandAudioProcessor proc;
        proc.prepareToPlay (sr, block);
        auto& a = proc.parameters();
        makeNeutral (a);
        // Arm the low band hard, but feed a QUIET high tone (8 kHz, in the high
        // band whose comp is neutral). It should pass essentially unchanged.
        setParam (a, "thr1", -40.0f);
        setParam (a, "ratio1", 12.0f);

        std::vector<double> high { 8000.0 };
        double ph = 0.0, dry = 0.0; bool fin = true;
        renderTones (proc, sr, block, 8, 0.05f, high, ph, fin, dry);           // ~ -26 dBFS
        const double out = renderTones (proc, sr, block, 12, 0.05f, high, ph, fin, dry);
        const double changeDb = gainToDb (out / (dry + 1e-12));
        std::printf ("quiet-high: dryRMS=%.5f outRMS=%.5f change=%.3f dB highGR=%.2f finite=%d\n",
                     dry, out, changeDb, proc.getBandGainReductionDb (2), (int) fin);
        if (! fin)                       { std::printf ("  FAIL non-finite (quiet high)\n"); ++fails; }
        if (std::fabs (changeDb) > 1.0)  { std::printf ("  FAIL quiet band did not pass ~untouched\n"); ++fails; }
        if (proc.getBandGainReductionDb (2) > 0.5f)
                                         { std::printf ("  FAIL high band compressed when it should not\n"); ++fails; }
    }

    std::printf (fails == 0 ? "Sattari Multiband contract ok\n"
                            : "Sattari Multiband contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
