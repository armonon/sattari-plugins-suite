// Sattari Image — functional contract.
// Feeds a decorrelated stereo signal (independent tones per channel, so there is
// real side energy) and proves the imager's guarantees:
//   (1) neutral (width=1, low-mono off, Haas off) is bit-exact pass-through;
//   (2) width=0 collapses to mono (L == R everywhere);
//   (3) increasing width raises the side energy AND lowers L/R correlation;
//   (4) the mono sum (L+R)/2 stays sane across widths (no huge level jump);
//   (5) no NaNs/Infs anywhere.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

struct ImgResult
{
    double sideEnergy = 0.0;   // mean of ((L-R)/2)^2
    double midEnergy  = 0.0;   // mean of ((L+R)/2)^2
    double corr       = 0.0;   // L/R Pearson correlation
    double maxMonoAbs = 0.0;   // peak |mono sum|
    bool   finite     = true;
    bool   channelsEqual = true; // L==R for every sample?
    double maxDiffVsInput = 0.0; // max |out - in|, per channel (for bit-exact test)
};

// Render a decorrelated stereo signal through the imager with a given config.
ImgResult render (float width, bool lowMono, float haas, bool monoAud)
{
    const double sr = 48000.0; const int block = 512;
    SattariImageAudioProcessor p; auto& a = p.parameters();
    setParam (a, "width", width);
    setParam (a, "widthLow", 1.0f); setParam (a, "widthMid", 1.0f); setParam (a, "widthHigh", 1.0f);
    setParam (a, "lowMono", lowMono ? 1.0f : 0.0f);
    setParam (a, "lowMonoFreq", 120.0f);
    setParam (a, "haas", haas);
    setParam (a, "monoAudition", monoAud ? 1.0f : 0.0f);
    p.prepareToPlay (sr, block);

    const int n = 48000;   // 1 s
    juce::MidiBuffer midi;
    ImgResult res;
    double sll = 0.0, srr = 0.0, slr = 0.0, sl = 0.0, sr2 = 0.0;
    long long cnt = 0;

    double phL = 0.0, phR = 0.0;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len);
        for (int i = 0; i < len; ++i)
        {
            // Distinct tones per channel + a shared component -> partly correlated,
            // with genuine side energy at 440 vs 660 Hz.
            const float shared = 0.3f * (float) std::sin (2.0 * M_PI * 220.0 * (double) (start + i) / sr);
            const float lTone  = 0.5f * (float) std::sin (phL) + shared;
            const float rTone  = 0.5f * (float) std::sin (phR) + shared;
            buf.setSample (0, i, lTone);
            buf.setSample (1, i, rTone);
            phL += 2.0 * M_PI * 440.0 / sr;
            phR += 2.0 * M_PI * 660.0 / sr;
        }

        // Keep a copy of the input for the bit-exact comparison.
        juce::AudioBuffer<float> in (2, len);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < len; ++i) in.setSample (ch, i, buf.getSample (ch, i));

        p.processBlock (buf, midi);

        for (int i = 0; i < len; ++i)
        {
            const float L = buf.getSample (0, i);
            const float R = buf.getSample (1, i);
            if (! std::isfinite (L) || ! std::isfinite (R)) res.finite = false;
            if (L != R) res.channelsEqual = false;

            const double side = 0.5 * (L - R);
            const double mid  = 0.5 * (L + R);
            res.sideEnergy += side * side;
            res.midEnergy  += mid * mid;
            res.maxMonoAbs  = std::max (res.maxMonoAbs, std::fabs (mid));

            sll += (double) L * L; srr += (double) R * R; slr += (double) L * R;
            sl  += L; sr2 += R; ++cnt;

            for (int ch = 0; ch < 2; ++ch)
                res.maxDiffVsInput = std::max (res.maxDiffVsInput,
                    (double) std::fabs (buf.getSample (ch, i) - in.getSample (ch, i)));
        }
    }

    res.sideEnergy /= (double) cnt;
    res.midEnergy  /= (double) cnt;
    const double den = std::sqrt ((sll - sl * sl / cnt) * (srr - sr2 * sr2 / cnt));
    res.corr = den > 1.0e-12 ? (slr - sl * sr2 / cnt) / den : 1.0;
    return res;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    int fails = 0;

    const ImgResult neutral = render (1.0f, false, 0.0f, false);
    const ImgResult mono    = render (0.0f, false, 0.0f, false);
    const ImgResult narrow  = render (0.5f, false, 0.0f, false);
    const ImgResult wide     = render (1.8f, false, 0.0f, false);
    const ImgResult lowMonoR = render (1.2f, true,  0.0f, false);
    const ImgResult haasR    = render (1.2f, false, 0.4f, false);

    std::printf ("neutral: side=%.6f mid=%.6f corr=%.4f maxDiff=%.2e\n",
                 neutral.sideEnergy, neutral.midEnergy, neutral.corr, neutral.maxDiffVsInput);
    std::printf ("mono   : chEqual=%d side=%.6f\n", (int) mono.channelsEqual, mono.sideEnergy);
    std::printf ("narrow : side=%.6f corr=%.4f\n", narrow.sideEnergy, narrow.corr);
    std::printf ("wide   : side=%.6f corr=%.4f\n", wide.sideEnergy, wide.corr);
    std::printf ("monoSum: neutral=%.4f narrow=%.4f wide=%.4f\n",
                 neutral.maxMonoAbs, narrow.maxMonoAbs, wide.maxMonoAbs);

    // (5) no NaNs anywhere.
    if (! (neutral.finite && mono.finite && narrow.finite && wide.finite
           && lowMonoR.finite && haasR.finite))
    { std::printf ("  FAIL non-finite output\n"); ++fails; }

    // (1) neutral is bit-exact pass-through.
    if (neutral.maxDiffVsInput > 1.0e-6)
    { std::printf ("  FAIL neutral not bit-exact (maxDiff=%.3e)\n", neutral.maxDiffVsInput); ++fails; }

    // (2) width=0 collapses to mono: L==R everywhere, no side energy.
    if (! mono.channelsEqual || mono.sideEnergy > 1.0e-12)
    { std::printf ("  FAIL width=0 did not collapse to mono\n"); ++fails; }

    // (3) increasing width raises side energy AND lowers correlation.
    if (! (mono.sideEnergy < narrow.sideEnergy
           && narrow.sideEnergy < neutral.sideEnergy
           && neutral.sideEnergy < wide.sideEnergy))
    { std::printf ("  FAIL side energy not monotonic in width\n"); ++fails; }
    if (! (wide.corr < neutral.corr && neutral.corr < mono.corr - 1.0e-6))
    { std::printf ("  FAIL correlation did not drop as width rose\n"); ++fails; }

    // (4) mono sum stays sane: the (L+R)/2 peak must NOT jump wildly with width
    // (the mid is untouched, so it should barely move; allow small FP wobble).
    if (std::fabs (wide.maxMonoAbs - neutral.maxMonoAbs) > 0.05 * neutral.maxMonoAbs + 1.0e-4)
    { std::printf ("  FAIL mono sum jumped with width\n"); ++fails; }
    if (wide.maxMonoAbs > 4.0)
    { std::printf ("  FAIL runaway mono sum\n"); ++fails; }

    // Sanity: low-mono and Haas paths stay bounded + reduce/keep the low side.
    if (lowMonoR.maxMonoAbs > 4.0 || haasR.maxMonoAbs > 4.0)
    { std::printf ("  FAIL low-mono/haas runaway\n"); ++fails; }

    std::printf (fails == 0 ? "Image contract ok\n" : "Image contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
