// Sattari Bus — core DSP contract.
//
// Three machine-checkable assertions on the real signal path (no ears required):
//   A) Width = 0 collapses a hard-panned input to mono (L ≈ R).
//   B) M/S Process round-trips transparently: a stereo input stays stereo and
//      is preserved (validates the msMode encode→decode commit).
//   C) Mute silences the output.
//
// All other processing is neutralised (drive 0, comp ratio 1, EQ flat, mix 100)
// so each assertion isolates the feature under test.
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    else
        std::fprintf (stderr, "WARNING: missing parameter %s\n", id.toRawUTF8());
}

// Neutralise everything except the feature being tested.
void neutralise (juce::AudioProcessorValueTreeState& apvts)
{
    setParam (apvts, "inputTrim", 0.0f);
    setParam (apvts, "hpf", 20.0f);
    setParam (apvts, "lpf", 20000.0f);
    setParam (apvts, "low", 0.0f);
    setParam (apvts, "mid", 0.0f);
    setParam (apvts, "high", 0.0f);
    setParam (apvts, "ratio", 1.0f);          // no compression
    setParam (apvts, "threshold", 0.0f);
    setParam (apvts, "makeup", 0.0f);
    setParam (apvts, "drive", 0.0f);          // no saturation
    setParam (apvts, "width", 1.0f);
    setParam (apvts, "pan", 0.0f);
    setParam (apvts, "mix", 100.0f);
    setParam (apvts, "output", 0.0f);
    setParam (apvts, "ceiling", 0.0f);
    setParam (apvts, "mute", 0.0f);
    setParam (apvts, "bypass", 0.0f);
    setParam (apvts, "msMode", 0.0f);
}

struct ChannelStats { double rmsL = 0.0, rmsR = 0.0, diff = 0.0; };

// Feed a hard-panned signal (L = sine, R = quieter sine) for a number of blocks
// and measure the steady-state output channel stats from the last block.
ChannelStats run (SattariBusAudioProcessor& proc, double sr, int block, int blocks,
                  float gainL, float gainR)
{
    double phase = 0.0;
    juce::AudioBuffer<float> last (2, block);
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, block);
        for (int i = 0; i < block; ++i)
        {
            const float s = (float) std::sin (phase);
            phase += juce::MathConstants<double>::twoPi * 220.0 / sr;
            buffer.setSample (0, i, gainL * s);
            buffer.setSample (1, i, gainR * s);
        }
        juce::MidiBuffer midi;
        proc.processBlock (buffer, midi);
        if (b == blocks - 1)
            last.makeCopyOf (buffer);
    }

    ChannelStats st;
    double sl = 0.0, sr2 = 0.0, sd = 0.0;
    for (int i = 0; i < block; ++i)
    {
        const double l = last.getSample (0, i);
        const double r = last.getSample (1, i);
        sl += l * l; sr2 += r * r; sd += (l - r) * (l - r);
    }
    st.rmsL = std::sqrt (sl / block);
    st.rmsR = std::sqrt (sr2 / block);
    st.diff = std::sqrt (sd / block);
    return st;
}
} // namespace

int main()
{
    const double sr = 48000.0;
    const int block = 512;
    int fails = 0;

    // --- A) Width = 0 collapses to mono -------------------------------------
    {
        SattariBusAudioProcessor proc;
        proc.prepareToPlay (sr, block);
        neutralise (proc.parameters());
        setParam (proc.parameters(), "width", 0.0f);
        const auto st = run (proc, sr, block, 20, 0.5f, 0.0f);   // hard-panned L
        std::printf ("A width=0: rmsL=%.4f rmsR=%.4f |L-R|=%.4f\n", st.rmsL, st.rmsR, st.diff);
        if (st.rmsL < 1.0e-3 || st.diff > 0.02)                  // both channels present, ~equal
            { std::fprintf (stderr, "FAIL A: width=0 did not collapse to mono\n"); ++fails; }
    }

    // --- B) M/S Process round-trips and preserves stereo --------------------
    {
        SattariBusAudioProcessor proc;
        proc.prepareToPlay (sr, block);
        neutralise (proc.parameters());
        setParam (proc.parameters(), "msMode", 1.0f);
        setParam (proc.parameters(), "width", 1.0f);
        const auto st = run (proc, sr, block, 20, 0.5f, 0.15f);  // genuinely stereo input
        std::printf ("B msMode=1: rmsL=%.4f rmsR=%.4f |L-R|=%.4f\n", st.rmsL, st.rmsR, st.diff);
        if (st.rmsL < 1.0e-3 || st.rmsR < 1.0e-3 || st.diff < 1.0e-3)
            { std::fprintf (stderr, "FAIL B: M/S round-trip collapsed or muted the signal\n"); ++fails; }
    }

    // --- C) Mute silences output --------------------------------------------
    {
        SattariBusAudioProcessor proc;
        proc.prepareToPlay (sr, block);
        neutralise (proc.parameters());
        setParam (proc.parameters(), "mute", 1.0f);
        const auto st = run (proc, sr, block, 20, 0.5f, 0.5f);
        std::printf ("C mute=1: rmsL=%.6f rmsR=%.6f\n", st.rmsL, st.rmsR);
        if (st.rmsL > 1.0e-4 || st.rmsR > 1.0e-4)
            { std::fprintf (stderr, "FAIL C: mute did not silence output\n"); ++fails; }
    }

    if (fails != 0) { std::fprintf (stderr, "Bus core contract FAILED (%d)\n", fails); return 1; }
    std::printf ("Bus core contract ok\n");
    return 0;
}
