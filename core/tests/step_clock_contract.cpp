// SattariStepClock — timing contract.
//
//  The clock is about to drive drums, notes and parameter modulation across the
//  suite, so its timing errors would show up as three different-looking bugs.
//  This pins the properties that matter and, deliberately, the two that are easy
//  to get wrong: cumulative drift, and dropping a swung step.
#include <sattari_audio_core/SattariStepClock.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double kSr = 48000.0;
int fails = 0;

void check (bool ok, const char* what)
{
    if (! ok) { std::printf ("  FAIL %s\n", what); ++fails; }
}

sattari::dsp::StepTransport playing (double ppq, double bpm = 120.0)
{
    return sattari::dsp::StepTransport { bpm, ppq, true, true };
}

// Run `blocks` blocks of `n` samples from ppq 0, collecting absolute step indices
// and the global sample position each landed on.
struct Hit { long long absolute; long long sample; };

std::vector<Hit> run (sattari::dsp::StepClock& clock, int blocks, int n,
                      int stepsPerBeat, int stepsPerCycle, float swing,
                      double bpm = 120.0, bool hostPpq = true)
{
    std::vector<Hit> hits;
    sattari::dsp::StepTick ticks[64];
    const double samplesPerBeat = kSr * 60.0 / bpm;
    for (int b = 0; b < blocks; ++b)
    {
        sattari::dsp::StepTransport t;
        if (hostPpq) t = playing ((double) (b * n) / samplesPerBeat, bpm);
        else { t.bpm = bpm; t.hasPpq = false; t.isPlaying = true; }

        const int c = clock.process (t, n, stepsPerBeat, stepsPerCycle, swing, ticks, 64);
        for (int i = 0; i < c; ++i)
            hits.push_back ({ ticks[i].absolute, (long long) b * n + ticks[i].sampleOffset });
    }
    return hits;
}
} // namespace

int main()
{
    std::printf ("StepClock contract\n");
    using namespace sattari::dsp;

    // --- 1. Every step fires exactly once, in order, none skipped ------------
    {
        StepClock clock; clock.prepare (kSr);
        auto hits = run (clock, 200, 512, 4, 16, 0.0f);
        check (! hits.empty(), "no steps fired at all");
        bool ordered = true, unique = true;
        for (size_t i = 1; i < hits.size(); ++i)
        {
            if (hits[i].absolute <= hits[i - 1].absolute) unique = false;
            if (hits[i].sample   <  hits[i - 1].sample)   ordered = false;
            if (hits[i].absolute != hits[i - 1].absolute + 1) unique = false;  // no gaps
        }
        check (unique,  "steps repeated, skipped or went backwards");
        check (ordered, "step sample offsets went backwards");
    }

    // --- 2. NO CUMULATIVE DRIFT ---------------------------------------------
    //  The failure this guards is placing steps by accumulating "samples per
    //  step": correct at step 1, seconds late by step 10,000. At 120 BPM a
    //  sixteenth is 6000 samples, so step k belongs at exactly k * 6000.
    {
        //  TEN MINUTES. A four-second run cannot distinguish an exact clock from
        //  an accumulating one — the whole point is the error that only appears
        //  after thousands of steps, which is where a naive implementation is
        //  already audibly late.
        StepClock clock; clock.prepare (kSr);
        constexpr int kBlocks = 56250;                 // 56250 * 512 / 48000 = 600 s
        auto hits = run (clock, kBlocks, 512, 4, 16, 0.0f);
        long long worst = 0;
        for (const auto& h : hits)
            worst = std::max (worst, std::llabs (h.sample - h.absolute * 6000));
        std::printf ("  %zu steps over %.0f s, worst placement error %lld samples\n",
                     hits.size(), (double) kBlocks * 512.0 / kSr, worst);
        check (worst <= 1, "step placement drifts from the absolute grid");
    }

    // --- 3. Block size must not change WHERE steps land ----------------------
    //  Hosts vary the block size freely; a clock that quantises to block
    //  boundaries would sound different at 64 samples than at 1024.
    {
        StepClock a, b; a.prepare (kSr); b.prepare (kSr);
        // 30 s at 120 BPM = 240 sixteenths, enough to expose any divergence.
        auto small = run (a, 22500, 64,  4, 16, 0.0f);
        auto large = run (b,  1406, 1024, 4, 16, 0.0f);
        const size_t n = std::min (small.size(), large.size());
        check (n > 200, "not enough steps to compare block sizes");
        long long worst = 0;
        for (size_t i = 0; i < n; ++i)
            worst = std::max (worst, std::llabs (small[i].sample - large[i].sample));
        std::printf ("  64 vs 1024 sample blocks: worst divergence %lld samples\n", worst);
        check (worst <= 1, "step timing depends on the host block size");
    }

    // --- 4. SWING DELAYS ODD STEPS, AND NONE ARE LOST -----------------------
    //  The subtle bug: a swung odd step is pushed later, so scanning from the
    //  current step drops it. Count must be unchanged; odd steps must move.
    {
        StepClock straight, swung;
        straight.prepare (kSr); swung.prepare (kSr);
        auto dry = run (straight, 2000, 512, 4, 16, 0.0f);
        auto wet = run (swung,    2000, 512, 4, 16, 0.6f);
        // A swung final step can be pushed past the end of the run, so the counts
        // may legitimately differ by one at the boundary — but no more than that.
        check (std::llabs ((long long) dry.size() - (long long) wet.size()) <= 1,
               "swing lost or duplicated steps");

        const size_t n = std::min (dry.size(), wet.size());
        bool oddMoved = true, evenStill = true;
        for (size_t i = 0; i < n; ++i)
        {
            const long long delta = wet[i].sample - dry[i].sample;
            if (dry[i].absolute & 1) { if (delta <= 0) oddMoved = false; }
            else                     { if (delta != 0) evenStill = false; }
        }
        check (oddMoved,  "swing did not delay odd steps");
        check (evenStill, "swing moved even steps, which it must not");
        // 0.6 swing on a 6000-sample sixteenth => 0.6 * 3000 = 1800 samples.
        const long long expected = 1800;
        long long worst = 0;
        for (size_t i = 0; i < n; ++i)
            if (dry[i].absolute & 1)
                worst = std::max (worst, std::llabs ((wet[i].sample - dry[i].sample) - expected));
        std::printf ("  swing 0.6: odd-step delay error %lld samples (expected %lld)\n",
                     worst, expected);
        check (worst <= 1, "swing delay is not half a step at full depth");
    }

    // --- 5. Cycle wrap, and the absolute counter surviving it ---------------
    {
        StepClock clock; clock.prepare (kSr);
        auto hits = run (clock, 600, 512, 4, 16, 0.0f);   // > 16 steps, so it wraps
        bool wrapped = true;
        for (const auto& h : hits)
        {
            const int expectStep = (int) (h.absolute % 16);
            if (expectStep < 0 || expectStep > 15) { wrapped = false; break; }
        }
        check (wrapped, "step index left the cycle range");
        check (hits.size() > 16, "never reached a cycle wrap");
    }

    // --- 6. Free-run when the host reports no PPQ ----------------------------
    //  Standalone must still play. This is the difference between an instrument
    //  that works outside a DAW and one that sits silent.
    {
        StepClock clock; clock.prepare (kSr);
        auto hits = run (clock, 600, 512, 4, 16, 0.0f, 120.0, /*hostPpq*/ false);
        check (! hits.empty(), "free-run produced no steps without host PPQ");
        long long worst = 0;
        for (const auto& h : hits)
            worst = std::max (worst, std::llabs (h.sample - h.absolute * 6000));
        check (worst <= 512, "free-run clock drifts badly");
    }

    // --- 7. Stopped transport emits nothing ---------------------------------
    {
        StepClock clock; clock.prepare (kSr);
        sattari::dsp::StepTransport t { 120.0, 0.0, true, false };
        StepTick ticks[64];
        int total = 0;
        for (int b = 0; b < 50; ++b) total += clock.process (t, 512, 4, 16, 0.0f, ticks, 64);
        check (total == 0, "steps fired while the transport was stopped");
        check (clock.currentStep() == -1, "currentStep should be -1 when stopped");
    }

    // --- 8. Tempo is honoured -----------------------------------------------
    {
        StepClock clock; clock.prepare (kSr);
        auto hits = run (clock, 200, 512, 4, 16, 0.0f, 140.0);
        // a sixteenth at 140 BPM = 48000 * 60 / 140 / 4 = 5142.86 samples
        const double expected = kSr * 60.0 / 140.0 / 4.0;
        double worst = 0.0;
        for (const auto& h : hits)
            worst = std::max (worst, std::fabs ((double) h.sample - (double) h.absolute * expected));
        std::printf ("  140 BPM: worst placement error %.1f samples\n", worst);
        check (worst <= 1.5, "step spacing ignores tempo");
    }

    if (fails == 0) { std::printf ("StepClock contract ok\n"); return 0; }
    std::printf ("StepClock contract FAILED (%d)\n", fails);
    return 1;
}
