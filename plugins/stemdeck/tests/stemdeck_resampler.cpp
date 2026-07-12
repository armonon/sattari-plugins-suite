// Resampler quality check. When a file's sample rate differs from the output
// rate, the deck reads at fractional positions. This measures the interpolation
// error (high-frequency droop + aliasing/imaging) of the old 2-point linear read
// vs the new 16-tap windowed-sinc read, against the exact analytic ideal.
//
// For a pure input sine sin(2*pi*f*k/fsIn), the correctly resampled output at
// out-sample n (reading input position p = n*fsIn/fsOut) is exactly
// sin(2*pi*f*n/fsOut). So we can compute the ideal output in closed form and
// report total error as 10*log10( sum(err^2) / sum(ideal^2) ) — lower is better.
#include "Interpolation.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace
{
constexpr double kPi = 3.14159265358979323846;

struct Err { double linearDb; double sincDb; };

Err measure (double freq, double fsIn, double fsOut, double seconds)
{
    const int nIn = (int) (fsIn * seconds);
    juce::AudioBuffer<float> in (1, nIn);
    auto* d = in.getWritePointer (0);
    for (int k = 0; k < nIn; ++k)
        d[k] = (float) std::sin (2.0 * kPi * freq * (double) k / fsIn);

    const double r = fsIn / fsOut;
    const int nOut = (int) (fsOut * seconds);
    // Guard the edges where the kernel would clamp against the buffer ends.
    const int guardOut = (int) std::ceil ((stemdeck::SincKernelTable::halfWidth + 2) / r) + 8;

    double eLin = 0.0, eSinc = 0.0, sig = 0.0;
    for (int n = guardOut; n < nOut - guardOut; ++n)
    {
        const double p     = (double) n * r;
        const double ideal = std::sin (2.0 * kPi * freq * (double) n / fsOut);
        const double lin   = (double) stemdeck::readLinear (in, 0, p);
        const double snc   = (double) stemdeck::readSinc   (in, 0, p);
        eLin  += (lin - ideal) * (lin - ideal);
        eSinc += (snc - ideal) * (snc - ideal);
        sig   += ideal * ideal;
    }
    const double norm = juce::jmax (sig, 1.0e-30);
    return { 10.0 * std::log10 (juce::jmax (eLin, 1.0e-30) / norm),
             10.0 * std::log10 (juce::jmax (eSinc, 1.0e-30) / norm) };
}
} // namespace

int main()
{
    const double fsIn = 44100.0, fsOut = 48000.0;   // the common 44.1k-on-48k case
    std::cout << "Resampling error vs analytic ideal (44100 -> 48000 Hz)\n";
    std::cout << "  freq(Hz)   linear(dB)   sinc(dB)   improvement\n";

    int fails = 0;
    for (double f : { 1000.0, 5000.0, 10000.0, 15000.0, 19000.0 })
    {
        const Err e = measure (f, fsIn, fsOut, 1.0);
        const double improve = e.linearDb - e.sincDb;
        std::printf ("  %7.0f   %9.2f   %8.2f   %+7.2f dB\n", f, e.linearDb, e.sincDb, improve);
        // Sinc must beat linear at EVERY frequency, and clearly (>=15 dB) and
        // cleanly (<-40 dB residual) across the audible band up to 15 kHz. The
        // top bin (19 kHz, right against the 22.05 kHz input Nyquist) rolls off
        // as any finite kernel must — we only require it still beats linear.
        if (e.sincDb >= e.linearDb) ++fails;
        if (f >= 5000.0 && f <= 15000.0 && improve < 15.0) ++fails;
        if (f <= 15000.0 && e.sincDb > -40.0) ++fails;
    }

    // Bit-exact guarantee: at matched rate, integer positions must return the
    // exact sample (sinc(0) is a unit delta) — no quality loss for plain playback.
    {
        juce::AudioBuffer<float> b (1, 512);
        auto* d = b.getWritePointer (0);
        for (int k = 0; k < 512; ++k) d[k] = (float) std::sin (0.017 * k) * 0.8f;
        double worst = 0.0;
        for (int k = 16; k < 512 - 16; ++k)
            worst = juce::jmax (worst, (double) std::abs (stemdeck::readSinc (b, 0, (double) k) - d[k]));
        std::printf ("  matched-rate integer read: max error %.2e  %s\n",
                     worst, worst < 1.0e-5 ? "(bit-exact)" : "(FAIL)");
        if (worst >= 1.0e-5) ++fails;
    }

    std::cout << (fails == 0 ? "RESAMPLER_OK\n" : "RESAMPLER_FAIL (" + std::to_string (fails) + ")\n");
    return fails == 0 ? 0 : 1;
}
