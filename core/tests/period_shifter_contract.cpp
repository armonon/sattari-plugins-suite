// PeriodShifter: the low-latency pitch path, measured against the number that matters.
//
// This exists to hold a competitive claim honest. Antares publishes 112 samples for
// Auto-Tune's low-latency mode (2.5 ms at 44.1 kHz) and roughly 37 for Classic. Our PSOLA
// path is 680. The whole reason PeriodShifter exists is to be in the first family, so the
// latency assertion here is a hard gate, not a print.
//
// Everything below is SYNTHETIC. Per CLAUDE.md this proves the mechanism executes and is
// arithmetically sound; it proves nothing about how it sounds on a real voice.

#include "sattari_audio_core/SattariPeriodShifter.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

std::vector<float> makeTone (double hz, double sr, int n, int harmonics = 4)
{
    std::vector<float> out ((size_t) n, 0.0f);
    for (int h = 1; h <= harmonics; ++h)
    {
        const double inc = 2.0 * M_PI * hz * (double) h / sr;
        const double amp = 0.5 / (double) h;
        double phase = 0.0;
        for (int i = 0; i < n; ++i) { out[(size_t) i] += (float) (amp * std::sin (phase)); phase += inc; }
    }
    return out;
}

// Zero crossings per second: a cheap, FFT-free fundamental proxy for a harmonic tone.
double crossingsPerSecond (const std::vector<float>& v, double sr, int from)
{
    int crossings = 0, counted = 0;
    for (size_t i = (size_t) from + 1; i < v.size(); ++i)
    {
        if ((v[i - 1] <= 0.0f) != (v[i] <= 0.0f)) ++crossings;
        ++counted;
    }
    return counted > 0 ? (double) crossings * sr / (double) counted : 0.0;
}
} // namespace

int main()
{
    constexpr double sr = 44100.0;
    constexpr int blockSize = 512;
    constexpr int blocks = 200;
    const int total = blockSize * blocks;

    std::printf ("--- Sattari period-shifter contract\n");

    sattari::dsp::PeriodShifter shifter;
    shifter.prepare (sr, 60.0, 1000.0);

    // ---- 1. THE LATENCY GATE ------------------------------------------------
    //
    // 150 samples is the bar: it puts us in Auto-Tune's family (112) rather than
    // PSOLA's (680), with margin for the interpolator. If a change pushes past this the
    // competitive claim is void and the build should say so.
    const int latency = shifter.getLatencySamples();
    std::printf ("  latency: %d samples = %.2f ms @ %.0f Hz\n",
                 latency, 1000.0 * latency / sr, sr);
    check (latency > 0 && latency <= 150,
           "latency is within the low-latency family (<= 150 samples)");

    // ---- 2. Unity is transparent, and delayed by exactly the reported latency ----
    {
        shifter.reset();
        const auto in = makeTone (220.0, sr, total);
        std::vector<float> out ((size_t) total, 0.0f);
        for (int b = 0; b < blocks; ++b)
            shifter.process (in.data() + b * blockSize, out.data() + b * blockSize, blockSize, 1.0);

        // Compare the tail against the input shifted by the reported latency. If the
        // reported figure were wrong this correlation collapses.
        double num = 0.0, da = 0.0, db = 0.0;
        for (int i = total / 2; i < total - latency; ++i)
        {
            const double a = out[(size_t) (i + latency)];
            const double b2 = in[(size_t) i];
            num += a * b2; da += a * a; db += b2 * b2;
        }
        const double corr = num / std::sqrt (std::max (1.0e-12, da * db));
        std::printf ("  unity correlation at reported latency: %.4f\n", corr);
        check (corr > 0.99, "at unity the output is the input delayed by exactly getLatencySamples()");
    }

    // ---- 3. It actually shifts pitch, in the right direction, by the right amount ----
    struct Case { double ratio; const char* name; };
    const Case cases[] = {
        { 1.059463, "+1 semitone" },
        { 1.122462, "+2 semitones" },
        { 0.943874, "-1 semitone" },
        { 0.890899, "-2 semitones" },
    };

    for (const auto& c : cases)
    {
        shifter.reset();
        const auto in = makeTone (220.0, sr, total);
        std::vector<float> out ((size_t) total, 0.0f);
        for (int b = 0; b < blocks; ++b)
            shifter.process (in.data() + b * blockSize, out.data() + b * blockSize, blockSize, c.ratio);

        const double baseRate = crossingsPerSecond (in, sr, total / 2);
        const double outRate  = crossingsPerSecond (out, sr, total / 2);
        const double measured = outRate / std::max (1.0e-9, baseRate);
        const double cents = 1200.0 * std::log2 (std::max (1.0e-9, measured / c.ratio));
        std::printf ("  %-14s target %.4f  measured %.4f  err %+.1f cents\n",
                     c.name, c.ratio, measured, cents);
        // Measured error is under 3 cents on every case, so 15 is a real gate with
        // headroom rather than a rubber stamp. It was 60 while the read pointer could
        // overrun the write head — that bug showed as +130 cents on +2 semitones, which
        // a loose bound would have hidden.
        check (std::abs (cents) < 15.0, c.name);
    }

    // ---- 4. The tracker finds the fundamental -------------------------------
    {
        shifter.reset();
        const auto in = makeTone (147.0, sr, total);   // D3
        std::vector<float> out ((size_t) total, 0.0f);
        for (int b = 0; b < blocks; ++b)
            shifter.process (in.data() + b * blockSize, out.data() + b * blockSize, blockSize, 1.0);

        const double period = shifter.detectedPeriodSamples();
        const double hz = period > 0.0 ? sr / period : 0.0;
        const double cents = 1200.0 * std::log2 (std::max (1.0e-9, hz / 147.0));
        std::printf ("  tracker: %.2f Hz (target 147.00), %+.1f cents, confidence %.3f\n",
                     hz, cents, shifter.detectionConfidence());
        check (std::abs (cents) < 50.0, "tracker locks the fundamental within 50 cents");
        check (shifter.detectionConfidence() > 0.5f, "voiced material reports real confidence");
    }

    // ---- 5. Silence must not produce noise or a fabricated period -----------
    {
        shifter.reset();
        std::vector<float> in ((size_t) total, 0.0f);
        std::vector<float> out ((size_t) total, 0.0f);
        for (int b = 0; b < blocks; ++b)
            shifter.process (in.data() + b * blockSize, out.data() + b * blockSize, blockSize, 1.25);

        double peak = 0.0;
        for (float v : out) peak = std::max (peak, (double) std::abs (v));
        check (peak < 1.0e-6, "silence in, silence out");
        check (shifter.detectionConfidence() < 0.5f, "silence does not report a confident period");
    }

    // ---- 6. Output stays finite and bounded under a pitch sweep -------------
    {
        shifter.reset();
        const auto in = makeTone (200.0, sr, total);
        std::vector<float> out ((size_t) total, 0.0f);
        bool finite = true;
        double peak = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            const double ratio = 0.8 + 0.4 * ((double) b / (double) blocks);   // 0.8 -> 1.2
            shifter.process (in.data() + b * blockSize, out.data() + b * blockSize, blockSize, ratio);
        }
        for (float v : out)
        {
            if (! std::isfinite (v)) finite = false;
            peak = std::max (peak, (double) std::abs (v));
        }
        std::printf ("  sweep peak: %.4f\n", peak);
        check (finite, "a continuous pitch sweep produces only finite samples");
        check (peak < 2.0, "a continuous pitch sweep does not blow up");
    }

    std::printf (failures == 0 ? "Sattari period-shifter contract ok\n"
                               : "Sattari period-shifter contract FAILED\n");
    return failures == 0 ? 0 : 1;
}
