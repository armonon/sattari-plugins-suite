// Sattari 808 — functional contract.
// Proves:
//   (1) blend=0 is an exact dry pass-through (the 808 body is silent).
//   (2) fed a steady low tone, the synthesized body's fundamental matches the
//       input note within a tolerance (audio-follow tuning works).
//   (3) the output is always bounded (never clips past the soft ceiling).
//   (4) no sample is ever non-finite (no NaN/Inf).
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// Render `hz` (0 = silence) through Sattari 808 with the given blend; capture the
// wet-vs-dry buffers and a running peak. `configure` lets a caller tweak params.
juce::AudioBuffer<float> render (double hz, double seconds, float blend, double& peakOut,
                                 juce::AudioBuffer<float>* dryOut = nullptr)
{
    const double sr = 48000.0; const int block = 512;
    Sattari808AudioProcessor p; auto& a = p.parameters();
    setParam (a, "blend", blend);
    setParam (a, "wave", 0.0f);        // sine body
    setParam (a, "snap", 0.0f);        // follow raw pitch (no bus key needed)
    setParam (a, "glide", 0.0f);       // near-instant lock
    setParam (a, "decay", 2.5f);       // long tail so the body sustains for measurement
    setParam (a, "drive", 0.2f);
    setParam (a, "subfold", 0.0f);     // pure fundamental for a clean pitch read
    setParam (a, "output", 1.0f);
    p.prepareToPlay (sr, block);

    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> out (2, n);
    if (dryOut != nullptr) dryOut->setSize (2, n);
    double ph = 0.0; peakOut = 0.0;
    juce::MidiBuffer midi;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len);
        for (int i = 0; i < len; ++i)
        {
            float s = 0.0f;
            if (hz > 0.0) { s = 0.4f * (float) std::sin (ph); ph += 2.0 * M_PI * hz / sr; }
            buf.setSample (0, i, s); buf.setSample (1, i, s);
            if (dryOut != nullptr) { dryOut->setSample (0, start + i, s); dryOut->setSample (1, start + i, s); }
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < len; ++i)
        {
            out.setSample (0, start + i, buf.getSample (0, i));
            out.setSample (1, start + i, buf.getSample (1, i));
            peakOut = std::max (peakOut, (double) std::fabs (buf.getSample (0, i)));
        }
    }
    return out;
}

// RMS + dominant frequency (from zero crossings) over the SECOND half (settled).
void measure (const juce::AudioBuffer<float>& b, double sr, double& rms, double& domHz)
{
    const int n = b.getNumSamples(); const int from = n / 2;
    const float* d = b.getReadPointer (0);
    double sumsq = 0.0; int crossings = 0; float prev = d[from];
    for (int i = from; i < n; ++i)
    {
        sumsq += (double) d[i] * d[i];
        if ((prev < 0.0f && d[i] >= 0.0f) || (prev > 0.0f && d[i] <= 0.0f)) ++crossings;
        prev = d[i];
    }
    const int cnt = n - from;
    rms = cnt ? std::sqrt (sumsq / cnt) : 0.0;
    const double secs = (double) cnt / sr;
    domHz = secs > 0.0 ? (crossings / 2.0) / secs : 0.0;   // half-crossings per second
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    const double sr = 48000.0;
    int fails = 0;

    // 1) blend=0 -> exact dry pass-through. The output must equal the dry input
    //    sample-for-sample (the 808 body contributes nothing).
    {
        double peak = 0.0;
        juce::AudioBuffer<float> dry;
        auto wet = render (60.0, 0.5, 0.0f, peak, &dry);
        double maxDiff = 0.0;
        for (int i = 0; i < wet.getNumSamples(); ++i)
            maxDiff = std::max (maxDiff, (double) std::fabs (wet.getSample (0, i) - dry.getSample (0, i)));
        std::printf ("blend0: maxDiff=%.2e peak=%.3f\n", maxDiff, peak);
        if (maxDiff > 1.0e-5)  { std::printf ("  FAIL blend=0 is not a clean pass-through\n"); ++fails; }
    }

    // 2) Steady low tone -> a synthesized, bounded body whose fundamental tracks
    //    the input note. Feed 55 Hz (A1); the body should land near 55 Hz.
    {
        const double inHz = 55.0;
        double peak = 0.0;
        auto body = render (inHz, 1.4, 1.0f, peak);   // wet-only so we measure the body
        double rms = 0.0, domHz = 0.0; measure (body, sr, rms, domHz);
        std::printf ("tone55: rms=%.4f domHz=%.1f peak=%.3f\n", rms, domHz, peak);

        bool finite = true;
        for (int i = 0; i < body.getNumSamples(); ++i)
            if (! std::isfinite (body.getSample (0, i))) finite = false;

        if (! finite)       { std::printf ("  FAIL non-finite output\n"); ++fails; }
        if (rms < 5.0e-3)   { std::printf ("  FAIL body silent (never retriggered?)\n"); ++fails; }
        // Tuning: dominant frequency within ~1.5 semitones of the 55 Hz input.
        const double ratio = domHz > 0.0 ? domHz / inHz : 0.0;
        const double cents = ratio > 0.0 ? 1200.0 * std::log2 (ratio) : 1.0e9;
        std::printf ("  tuning: %.1f Hz vs %.1f Hz (%.0f cents)\n", domHz, inHz, cents);
        if (std::fabs (cents) > 150.0) { std::printf ("  FAIL body fundamental does not track input note\n"); ++fails; }
        if (peak > 1.0)     { std::printf ("  FAIL output clips past soft ceiling\n"); ++fails; }
    }

    // 3) A loud, aggressive setting must still stay bounded and finite.
    {
        const double sr2 = 48000.0; const int block = 512;
        Sattari808AudioProcessor p; auto& a = p.parameters();
        setParam (a, "blend", 1.0f);
        setParam (a, "drive", 1.0f);
        setParam (a, "subfold", 1.0f);
        setParam (a, "output", 2.0f);
        setParam (a, "decay", 2.5f);
        p.prepareToPlay (sr2, block);
        const int n = (int) (sr2 * 0.8);
        double ph = 0.0, peak = 0.0; bool finite = true;
        juce::MidiBuffer midi;
        for (int start = 0; start < n; start += block)
        {
            const int len = std::min (block, n - start);
            juce::AudioBuffer<float> buf (2, len);
            for (int i = 0; i < len; ++i)
            {
                const float s = 0.9f * (float) std::sin (ph); ph += 2.0 * M_PI * 50.0 / sr2;
                buf.setSample (0, i, s); buf.setSample (1, i, s);
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < len; ++i)
            {
                const float v = buf.getSample (0, i);
                if (! std::isfinite (v)) finite = false;
                peak = std::max (peak, (double) std::fabs (v));
            }
        }
        std::printf ("hot: peak=%.3f finite=%d\n", peak, (int) finite);
        if (! finite)   { std::printf ("  FAIL non-finite under hot drive\n"); ++fails; }
        if (peak > 1.0) { std::printf ("  FAIL output unbounded under hot drive\n"); ++fails; }
    }

    std::printf (fails == 0 ? "Sattari 808 contract ok\n" : "Sattari 808 contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
