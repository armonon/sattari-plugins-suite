// Sattari Glass Choir — functional contract.
// Mirrors the double/granulator harness. Proves the 3-bus ensemble render, the
// single-out fold-down fallback, the character macro bounded across 3 modes,
// mono-safe width, atmosphere ducking, and honest reported latency.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double kSr    = 48000.0;
constexpr int    kBlock = 512;

void setParam (juce::AudioProcessorValueTreeState& a, const char* id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

double rms (const float* d, int n)
{
    double s = 0.0; for (int i = 0; i < n; ++i) s += (double) d[i] * d[i];
    return n > 0 ? std::sqrt (s / n) : 0.0;
}

// Fill the INPUT bus of `buf` with a voiced tone (fundamental + harmonics) + a
// little noise, so the pitch detector locks and the ensemble has something to
// chew. Returns the phase advanced.
double fillInput (SattariGlassChoirAudioProcessor& p, juce::AudioBuffer<float>& buf,
                  double ph, double freq, float amp)
{
    auto inBus = p.getBusBuffer (buf, true, 0);
    const int n = buf.getNumSamples();
    const double inc = 2.0 * juce::MathConstants<double>::pi * freq / kSr;
    juce::Random rng (0x1234);
    for (int ch = 0; ch < inBus.getNumChannels(); ++ch)
    {
        float* d = inBus.getWritePointer (ch);
        double lp = ph;
        for (int i = 0; i < n; ++i)
        {
            const float voiced = amp * (float) (std::sin (lp) + 0.4 * std::sin (2.0 * lp) + 0.2 * std::sin (3.0 * lp));
            d[i] = voiced + 0.01f * (rng.nextFloat() * 2.0f - 1.0f);
            lp += inc;
        }
    }
    return ph + inc * n;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    int fails = 0;
    using P = SattariGlassChoirAudioProcessor;

    // ---- 1. bypass=true (& macro=0, sections off) -> main ~= input -----------
    {
        P p; auto& a = p.parameters();
        setParam (a, "bypass", 1.0f);
        setParam (a, "character", 0.0f);
        setParam (a, "harmonyOn", 0.0f);
        setParam (a, "destroyOn", 0.0f);
        setParam (a, "trailsOn", 0.0f);
        p.enableAllBuses();
        p.prepareToPlay (kSr, kBlock);
        const int totalCh = juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));

        double ph = 0.0, sumIn = 0.0, sumDiff = 0.0; bool finite = true;
        for (int b = 0; b < 20; ++b)
        {
            juce::AudioBuffer<float> buf (totalCh, kBlock); buf.clear();
            ph = fillInput (p, buf, ph, 220.0, 0.5f);
            // Copy the input before processing (input aliases main out).
            std::vector<float> inCopy (kBlock);
            auto inBus = p.getBusBuffer (buf, true, 0);
            for (int i = 0; i < kBlock; ++i) inCopy[(size_t) i] = inBus.getReadPointer (0)[i];
            juce::MidiBuffer midi; p.processBlock (buf, midi);
            auto leadBus = p.getBusBuffer (buf, false, 0);
            for (int i = 0; i < kBlock; ++i)
            {
                const float y = leadBus.getReadPointer (0)[i];
                if (! std::isfinite (y)) finite = false;
                sumIn   += (double) inCopy[(size_t) i] * inCopy[(size_t) i];
                sumDiff += (double) (y - inCopy[(size_t) i]) * (y - inCopy[(size_t) i]);
            }
        }
        const double rel = sumIn > 0 ? std::sqrt (sumDiff / sumIn) : 0.0;
        std::printf ("1 bypass: rel-error=%.5e finite=%d\n", rel, (int) finite);
        if (! finite)      { std::printf ("  FAIL bypass non-finite\n"); ++fails; }
        // Bypass is a bit-exact dry copy of the (unity-gained) input, so the
        // residual is effectively zero — no added ensemble/reverb energy.
        if (rel > 1.0e-5)  { std::printf ("  FAIL bypass main != input (added energy, rel=%.3e)\n", rel); ++fails; }
    }

    // ---- 2. 3 buses enabled -> Lead~=dry, Choir/Atmos non-zero & != Lead;
    //         aux disabled -> their energy folds into Lead ---------------------
    {
        // (a) all three enabled.
        P p; auto& a = p.parameters();
        setParam (a, "bypass", 0.0f);
        setParam (a, "character", 0.5f);
        setParam (a, "harmonyOn", 1.0f);
        setParam (a, "reverbMix", 0.5f);
        p.enableAllBuses();
        p.prepareToPlay (kSr, kBlock);
        const int totalCh = juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));

        double ph = 0.0, leadR = 0.0, choirR = 0.0, atmosR = 0.0; int cnt = 0; bool finite = true;
        for (int b = 0; b < 60; ++b)
        {
            juce::AudioBuffer<float> buf (totalCh, kBlock); buf.clear();
            ph = fillInput (p, buf, ph, 220.0, 0.4f);
            juce::MidiBuffer midi; p.processBlock (buf, midi);
            if (b >= 20)   // let detectors + reverb settle
            {
                auto lead  = p.getBusBuffer (buf, false, 0);
                auto choir = p.getBusBuffer (buf, false, 1);
                auto atmos = p.getBusBuffer (buf, false, 2);
                leadR  += rms (lead.getReadPointer (0), kBlock);
                choirR += rms (choir.getReadPointer (0), kBlock);
                atmosR += rms (atmos.getReadPointer (0), kBlock);
                for (int i = 0; i < kBlock; ++i)
                    if (! std::isfinite (lead.getReadPointer (0)[i])
                        || ! std::isfinite (choir.getReadPointer (0)[i])
                        || ! std::isfinite (atmos.getReadPointer (0)[i])) finite = false;
                ++cnt;
            }
        }
        leadR /= cnt; choirR /= cnt; atmosR /= cnt;
        std::printf ("2a buses: leadR=%.4f choirR=%.4f atmosR=%.4f finite=%d\n", leadR, choirR, atmosR, (int) finite);
        if (! finite)         { std::printf ("  FAIL bus render non-finite\n"); ++fails; }
        if (choirR <= 1e-4)   { std::printf ("  FAIL choir silent\n"); ++fails; }
        if (atmosR <= 1e-4)   { std::printf ("  FAIL atmos silent\n"); ++fails; }
        if (std::fabs (choirR - leadR) < 1e-4) { std::printf ("  FAIL choir == lead\n"); ++fails; }

        // (b) aux disabled -> their energy folds into Lead (single-out host).
        P q; auto& b2 = q.parameters();
        setParam (b2, "bypass", 0.0f);
        setParam (b2, "character", 0.5f);
        setParam (b2, "harmonyOn", 1.0f);
        setParam (b2, "reverbMix", 0.5f);
        // Only the main out bus enabled (mirrors a single-out host).
        q.prepareToPlay (kSr, kBlock);   // default layout: aux buses inactive
        const int qTotalCh = juce::jmax (2, juce::jmax (q.getTotalNumInputChannels(), q.getTotalNumOutputChannels()));
        double ph2 = 0.0, foldR = 0.0; int cnt2 = 0;
        for (int b = 0; b < 60; ++b)
        {
            juce::AudioBuffer<float> buf (qTotalCh, kBlock); buf.clear();
            ph2 = fillInput (q, buf, ph2, 220.0, 0.4f);
            juce::MidiBuffer midi; q.processBlock (buf, midi);
            if (b >= 20)
            {
                auto lead = q.getBusBuffer (buf, false, 0);
                foldR += rms (lead.getReadPointer (0), kBlock);
                ++cnt2;
            }
        }
        foldR /= cnt2;
        std::printf ("2b fold: single-out leadR=%.4f (>= lead-only %.4f)\n", foldR, leadR);
        // With choir+atmos folded in, the single-out lead carries clearly more
        // energy than the lead-only bus from case (a).
        if (foldR <= leadR * 1.2) { std::printf ("  FAIL aux did not fold into lead\n"); ++fails; }
    }

    // ---- 3. sweep character 0..1 across 3 modes -> finite & |x|<=~2.0 --------
    {
        bool finite = true; double maxAbs = 0.0;
        for (int mode = 0; mode < 3; ++mode)
        {
            for (int step = 0; step <= 8; ++step)
            {
                P p; auto& a = p.parameters();
                setParam (a, "bypass", 0.0f);
                setParam (a, "mode", (float) mode);
                setParam (a, "character", (float) step / 8.0f);
                setParam (a, "harmonyOn", 1.0f);
                setParam (a, "destroyOn", 1.0f);
                setParam (a, "trailsOn", 1.0f);
                p.enableAllBuses();
                p.prepareToPlay (kSr, kBlock);
                const int totalCh = juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));
                double ph = 0.0;
                juce::Random rng (0xBEEF);
                for (int b = 0; b < 24; ++b)
                {
                    juce::AudioBuffer<float> buf (totalCh, kBlock); buf.clear();
                    // Full-scale sine + noise on the input.
                    auto inBus = p.getBusBuffer (buf, true, 0);
                    const double inc = 2.0 * juce::MathConstants<double>::pi * 330.0 / kSr;
                    for (int ch = 0; ch < inBus.getNumChannels(); ++ch)
                    {
                        float* d = inBus.getWritePointer (ch); double lp = ph;
                        for (int i = 0; i < kBlock; ++i) { d[i] = 0.9f * (float) std::sin (lp) + 0.1f * (rng.nextFloat()*2-1); lp += inc; }
                    }
                    ph += inc * kBlock;
                    juce::MidiBuffer midi; p.processBlock (buf, midi);
                    for (int bus = 0; bus < 3; ++bus)
                    {
                        auto bb = p.getBusBuffer (buf, false, bus);
                        for (int ch = 0; ch < bb.getNumChannels(); ++ch)
                            for (int i = 0; i < kBlock; ++i)
                            {
                                const float y = bb.getReadPointer (ch)[i];
                                if (! std::isfinite (y)) finite = false;
                                maxAbs = std::max (maxAbs, (double) std::fabs (y));
                            }
                    }
                }
            }
        }
        std::printf ("3 sweep: finite=%d maxAbs=%.4f\n", (int) finite, maxAbs);
        if (! finite)       { std::printf ("  FAIL sweep non-finite\n"); ++fails; }
        if (maxAbs > 2.05)  { std::printf ("  FAIL sweep unbounded (>2.0)\n"); ++fails; }
    }

    // ---- 4. mono-safe width: high width still has mono energy; lowMono strips
    //         low-band side; monoAudition -> L==R (on the choir bus) -----------
    {
        auto runWidth = [] (float width, bool lowMono, bool monoAud, double& midR, double& sideR, double& lrDiff)
        {
            P p; auto& a = p.parameters();
            setParam (a, "bypass", 0.0f);
            setParam (a, "character", 0.3f);
            setParam (a, "width", width);
            setParam (a, "lowMono", lowMono ? 1.0f : 0.0f);
            // Corner well ABOVE the source fundamental so the low-mono HP clearly
            // strips the low-band SIDE (the ensemble's decorrelation lives there).
            setParam (a, "lowMonoFreq", 200.0f);
            setParam (a, "monoAudition", monoAud ? 1.0f : 0.0f);
            p.enableAllBuses();
            p.prepareToPlay (kSr, kBlock);
            const int totalCh = juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));
            double ph = 0.0; midR = sideR = lrDiff = 0.0; int cnt = 0;
            for (int b = 0; b < 50; ++b)
            {
                juce::AudioBuffer<float> buf (totalCh, kBlock); buf.clear();
                // A LOW fundamental (80 Hz) so the widened side sits below the
                // 200 Hz low-mono corner and is removed when lowMono engages.
                ph = fillInput (p, buf, ph, 80.0, 0.4f);
                juce::MidiBuffer midi; p.processBlock (buf, midi);
                if (b >= 20)
                {
                    auto choir = p.getBusBuffer (buf, false, 1);
                    const float* L = choir.getReadPointer (0);
                    const float* R = choir.getReadPointer (choir.getNumChannels() > 1 ? 1 : 0);
                    std::vector<float> mid (kBlock), side (kBlock);
                    for (int i = 0; i < kBlock; ++i) { mid[(size_t)i] = 0.5f*(L[i]+R[i]); side[(size_t)i] = 0.5f*(L[i]-R[i]); }
                    midR  += rms (mid.data(), kBlock);
                    sideR += rms (side.data(), kBlock);
                    double d = 0.0; for (int i = 0; i < kBlock; ++i) d += std::fabs (L[i] - R[i]);
                    lrDiff += d / kBlock;
                    ++cnt;
                }
            }
            midR /= cnt; sideR /= cnt; lrDiff /= cnt;
        };

        double midWide, sideWide, lrWide; runWidth (1.9f, false, false, midWide, sideWide, lrWide);
        double midMono, sideMono, lrMono; runWidth (1.9f, false, true,  midMono, sideMono, lrMono);
        double midLM, sideLM, lrLM;       runWidth (1.9f, true,  false, midLM, sideLM, lrLM);
        std::printf ("4 width: wide mid=%.4f side=%.4f | monoAud lrDiff=%.6f | lowMono side=%.4f (< wide %.4f)\n",
                     midWide, sideWide, lrMono, sideLM, sideWide);
        if (midWide <= 1e-4)         { std::printf ("  FAIL high width lost mono energy\n"); ++fails; }
        if (sideWide <= 1e-4)        { std::printf ("  FAIL no widened side energy to low-mono\n"); ++fails; }
        if (lrMono > 1e-6)           { std::printf ("  FAIL monoAudition L != R (diff=%.3e)\n", lrMono); ++fails; }
        // The 80 Hz fundamental + 160 Hz harmonic side content sits below the
        // 200 Hz corner, so lowMono must strip a clear majority of the side.
        if (sideLM >= sideWide * 0.7){ std::printf ("  FAIL lowMono did not remove the low-band side (off=%.4f on=%.4f)\n", sideWide, sideLM); ++fails; }
    }

    // ---- 5. ducking: loud lead + reverb -> Atmos tracks the lead envelope
    //         (dips during loud lead vs. silence). ------------------------------
    {
        P p; auto& a = p.parameters();
        setParam (a, "bypass", 0.0f);
        setParam (a, "character", 0.4f);
        setParam (a, "reverbMix", 0.7f);
        setParam (a, "duckAmount", 0.9f);
        setParam (a, "duckAttackMs", 5.0f);
        setParam (a, "duckReleaseMs", 120.0f);
        p.enableAllBuses();
        p.prepareToPlay (kSr, kBlock);
        const int totalCh = juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));

        // Warm up the reverb tail with a loud lead, then measure atmos energy in a
        // LOUD window vs. a SILENT window (reverb still ringing but lead gone).
        double ph = 0.0, atmosLoud = 0.0, atmosSilent = 0.0; int cntL = 0, cntS = 0;
        for (int b = 0; b < 120; ++b)
        {
            juce::AudioBuffer<float> buf (totalCh, kBlock); buf.clear();
            const bool loud = (b < 80);
            if (loud) ph = fillInput (p, buf, ph, 220.0, 0.7f);   // else silence
            juce::MidiBuffer midi; p.processBlock (buf, midi);
            auto atmos = p.getBusBuffer (buf, false, 2);
            const double r = rms (atmos.getReadPointer (0), kBlock);
            if (loud && b >= 40)      { atmosLoud   += r; ++cntL; }
            else if (! loud && b < 92) { atmosSilent += r; ++cntS; }   // right after lead stops
        }
        atmosLoud   /= juce::jmax (1, cntL);
        atmosSilent /= juce::jmax (1, cntS);
        std::printf ("5 duck: atmos loud=%.5f silent=%.5f (loud should be ducked lower)\n", atmosLoud, atmosSilent);
        // The atmosphere is ducked while the lead is loud, so it should be QUIETER
        // during the loud window than just after the lead releases (tail un-ducks).
        if (atmosSilent <= atmosLoud) { std::printf ("  FAIL ducking did not track the lead\n"); ++fails; }
    }

    // ---- 6. latency honesty: reported getLatencySamples() == dry impulse delay
    {
        P p; auto& a = p.parameters();
        setParam (a, "bypass", 0.0f);
        setParam (a, "character", 0.0f);
        setParam (a, "harmonyOn", 0.0f);
        setParam (a, "destroyOn", 0.0f);
        setParam (a, "trailsOn", 0.0f);
        setParam (a, "reverbMix", 0.0f);
        setParam (a, "ensembleLevel", -60.0f);   // near-mute the ensemble so lead dominates
        setParam (a, "leadLevel", 0.0f);
        p.enableAllBuses();
        p.prepareToPlay (kSr, kBlock);
        const int reported = p.getLatencySamples();
        const int totalCh = juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));

        // Feed a single impulse; find where the lead bus first responds.
        const int nBlocks = juce::jmax (8, (reported / kBlock) + 6);
        std::vector<float> leadOut ((size_t) (nBlocks * kBlock), 0.0f);
        int global = 0;
        for (int b = 0; b < nBlocks; ++b)
        {
            juce::AudioBuffer<float> buf (totalCh, kBlock); buf.clear();
            auto inBus = p.getBusBuffer (buf, true, 0);
            if (b == 0) for (int ch = 0; ch < inBus.getNumChannels(); ++ch) inBus.getWritePointer (ch)[0] = 1.0f;
            juce::MidiBuffer midi; p.processBlock (buf, midi);
            auto lead = p.getBusBuffer (buf, false, 0);
            for (int i = 0; i < kBlock; ++i) leadOut[(size_t) (global + i)] = lead.getReadPointer (0)[i];
            global += kBlock;
        }
        int firstNonZero = -1;
        for (int i = 0; i < (int) leadOut.size(); ++i)
            if (std::fabs (leadOut[(size_t) i]) > 1.0e-4f) { firstNonZero = i; break; }
        std::printf ("6 latency: reported=%d observed=%d\n", reported, firstNonZero);
        if (firstNonZero < 0)                       { std::printf ("  FAIL no impulse observed\n"); ++fails; }
        else if (std::abs (firstNonZero - reported) > 2) { std::printf ("  FAIL reported latency != observed\n"); ++fails; }
    }

    std::printf (fails == 0 ? "Glass Choir contract ok\n" : "Glass Choir contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
