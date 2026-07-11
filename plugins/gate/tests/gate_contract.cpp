// Sattari Gate — functional contract.
//
// Proves the gate behaves like a gate, and only when it should:
//   (1) a HOT signal above threshold passes ~unchanged (gate open, near unity);
//   (2) a QUIET signal below threshold is attenuated by ~range (gate closed);
//   (3) hysteresis prevents chatter — a signal dithering right around a single
//       threshold does NOT toggle the gate open/closed every block (a plain
//       single-threshold gate would rapidly flap);
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

// Render a steady sine at peak amplitude `amp`; return the settled output RMS and
// whether every sample stayed finite. `dryRms` returns the (settled) input RMS.
double renderTone (SattariGateAudioProcessor& proc, double sr, int block, int blocks,
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

// Base gate settings: hard gate, no lookahead, quick edges. `thr` is the threshold.
void configure (juce::AudioProcessorValueTreeState& a, float thr, float rangeDb)
{
    setParam (a, "threshold", thr);
    setParam (a, "range", rangeDb);
    setParam (a, "ratio", 2.0f);
    setParam (a, "attack", 0.5f);
    setParam (a, "hold", 40.0f);
    setParam (a, "release", 60.0f);
    setParam (a, "lookahead", 0.0f);
    setParam (a, "hysteresis", 6.0f);
    setParam (a, "scHpf", 20.0f);       // detector HP effectively bypassed
    setParam (a, "scLpf", 20000.0f);    // detector LP effectively bypassed
    setParam (a, "scListen", 0.0f);
    setParam (a, "detMode", 0.0f);      // Peak
    setParam (a, "expander", 0.0f);     // hard gate
    setParam (a, "tempoGate", 0.0f);
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

    // --- (1) Above threshold: gate open, near pass-through -------------------
    {
        SattariGateAudioProcessor proc;
        proc.prepareToPlay (sr, block);
        configure (proc.parameters(), -40.0f, 60.0f);
        // Hot tone: peak 0.5 (~ -6 dBFS), far above a -40 dBFS threshold.
        double ph = 0.0, dry = 0.0; bool fin = true;
        renderTone (proc, sr, block, 8, 0.5f, freq, ph, fin, dry);      // warm up
        const double out = renderTone (proc, sr, block, 20, 0.5f, freq, ph, fin, dry);
        const double changeDb = gainToDb (out / (dry + 1e-12));
        std::printf ("above-threshold: dryRMS=%.5f outRMS=%.5f change=%.2f dB open=%d finite=%d\n",
                     dry, out, changeDb, (int) proc.getGateOpen(), (int) fin);
        if (! fin)                       { std::printf ("  FAIL non-finite (above thr)\n"); ++fails; }
        if (std::fabs (changeDb) > 0.5)  { std::printf ("  FAIL above-threshold signal not ~unchanged\n"); ++fails; }
        if (! proc.getGateOpen())        { std::printf ("  FAIL gate did not open on a hot signal\n"); ++fails; }
    }

    // --- (2) Below threshold: gate closed, attenuated by ~range -------------
    {
        SattariGateAudioProcessor proc;
        proc.prepareToPlay (sr, block);
        const float rangeDb = 48.0f;
        configure (proc.parameters(), -30.0f, rangeDb);
        setParam (proc.parameters(), "hold", 0.0f);      // let it close promptly
        setParam (proc.parameters(), "release", 20.0f);
        // Quiet tone: peak 0.01 (~ -40 dBFS), well below the -30 dBFS threshold.
        double ph = 0.0, dry = 0.0; bool fin = true;
        renderTone (proc, sr, block, 20, 0.01f, freq, ph, fin, dry);    // warm up / settle closed
        const double out = renderTone (proc, sr, block, 30, 0.01f, freq, ph, fin, dry);
        const double attenDb = -gainToDb (out / (dry + 1e-12));   // positive = attenuation
        std::printf ("below-threshold: dryRMS=%.6f outRMS=%.6f atten=%.1f dB (range=%.0f) open=%d finite=%d\n",
                     dry, out, attenDb, rangeDb, (int) proc.getGateOpen(), (int) fin);
        if (! fin)                    { std::printf ("  FAIL non-finite (below thr)\n"); ++fails; }
        if (proc.getGateOpen())       { std::printf ("  FAIL gate stayed open on a quiet signal\n"); ++fails; }
        // Expect roughly `range` dB of attenuation (allow a wide window for edge ramps).
        if (attenDb < rangeDb * 0.6)  { std::printf ("  FAIL quiet signal not attenuated near range\n"); ++fails; }
    }

    // --- (3) Hysteresis prevents chatter ------------------------------------
    //  Feed a signal whose level sits right AT the open threshold and dithers a
    //  couple of dB either side of it each block. With a single-threshold gate
    //  this would toggle open/closed constantly; the dual-threshold hysteresis
    //  (+ hold) must keep the state stable. We count how many times the published
    //  gate-open state flips across many blocks.
    {
        SattariGateAudioProcessor proc;
        proc.prepareToPlay (sr, block);
        const float thr = -24.0f;
        configure (proc.parameters(), thr, 60.0f);
        setParam (proc.parameters(), "hysteresis", 8.0f);   // wide gap
        setParam (proc.parameters(), "hold", 60.0f);
        setParam (proc.parameters(), "attack", 0.5f);
        setParam (proc.parameters(), "release", 200.0f);

        // Amplitude that sits right at threshold: -24 dBFS -> peak ~0.0631.
        const float atThr = (float) std::pow (10.0, thr / 20.0);
        double ph = 0.0, dry = 0.0; bool fin = true;

        // Warm up loud so the gate is definitely open first.
        renderTone (proc, sr, block, 8, atThr * 4.0f, freq, ph, fin, dry);

        int flips = 0;
        bool prevOpen = proc.getGateOpen();
        const int ditherBlocks = 60;
        for (int bi = 0; bi < ditherBlocks; ++bi)
        {
            // Alternate a hair above / a hair below the OPEN threshold every block.
            const float mult = (bi % 2 == 0) ? 1.12f : 0.90f;   // ~ +1 / -0.9 dB
            renderTone (proc, sr, block, 1, atThr * mult, freq, ph, fin, dry);
            const bool nowOpen = proc.getGateOpen();
            if (nowOpen != prevOpen) ++flips;
            prevOpen = nowOpen;
        }
        std::printf ("hysteresis: dither around threshold over %d blocks -> %d state flips finite=%d\n",
                     ditherBlocks, flips, (int) fin);
        if (! fin)          { std::printf ("  FAIL non-finite (hysteresis)\n"); ++fails; }
        // A chattering single-threshold gate would flip on most of the 60 blocks.
        // Hysteresis + hold must keep it near-stable: allow only a handful.
        if (flips > 4)      { std::printf ("  FAIL gate chattered (%d flips) despite hysteresis\n", flips); ++fails; }
    }

    // --- (4) Expander mode stays finite and engages -------------------------
    {
        SattariGateAudioProcessor proc;
        proc.prepareToPlay (sr, block);
        configure (proc.parameters(), -30.0f, 40.0f);
        setParam (proc.parameters(), "expander", 1.0f);
        setParam (proc.parameters(), "hold", 0.0f);
        setParam (proc.parameters(), "release", 20.0f);
        double ph = 0.0, dry = 0.0; bool fin = true;
        renderTone (proc, sr, block, 20, 0.02f, freq, ph, fin, dry);    // below threshold
        const double out = renderTone (proc, sr, block, 20, 0.02f, freq, ph, fin, dry);
        const double attenDb = -gainToDb (out / (dry + 1e-12));
        std::printf ("expander: dryRMS=%.6f outRMS=%.6f atten=%.1f dB finite=%d\n", dry, out, attenDb, (int) fin);
        if (! fin)             { std::printf ("  FAIL non-finite (expander)\n"); ++fails; }
        if (attenDb < 1.0)     { std::printf ("  FAIL expander did not attenuate below threshold\n"); ++fails; }
    }

    std::printf (fails == 0 ? "Sattari Gate contract ok\n" : "Sattari Gate contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
