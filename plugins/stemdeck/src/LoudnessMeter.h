#pragma once

// =============================================================================
//  LoudnessMeter — integrated loudness (LUFS) + true-peak, per ITU-R BS.1770 /
//  EBU R128 (brief §9 must-build: loudness / true-peak metering).
// -----------------------------------------------------------------------------
//  K-weighting = a high-shelf pre-filter (~+4 dB above ~1.7 kHz) then an RLB
//  high-pass (~38 Hz). Integrated loudness gates 400 ms blocks (75% overlap) at
//  the absolute -70 LUFS floor and a -10 LU relative gate. True-peak is the max
//  of the signal 4x-oversampled (catches inter-sample overshoot the sample peak
//  misses). Offline; used to meter a render before/at export.
//
//  KNOWN LIMITATION: true-peak oversamples with the 16-tap sinc (Interpolation.h),
//  which is accurate below ~0.3*Nyquist but rolls off toward Nyquist. On very
//  bright (>~15 kHz) content it can UNDER-read the inter-sample peak — a fully
//  BS.1770-compliant true-peak meter needs a dedicated flat-to-Nyquist 4x FIR.
//  Fine for loudness targeting; do not rely on it as the sole clip guard for
//  mastering-grade material until the oversampler is upgraded.
// =============================================================================

#include "Interpolation.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>
#include <algorithm>

namespace stemdeck
{

class LoudnessMeter
{
public:
    struct Result
    {
        double integratedLufs = -300.0;   // -300 = effectively silent
        double truePeakDb = -300.0;
    };

    static Result measure (const juce::AudioBuffer<float>& audio, double sr)
    {
        Result r;
        const int n = audio.getNumSamples();
        const int ch = juce::jmax (1, audio.getNumChannels());
        if (n <= 0 || sr <= 0.0) return r;

        // ── K-weight each channel ──────────────────────────────────────────────
        Biquad shelf, hp;
        shelf.highShelf (1681.9744509555319, 3.99984385397, 0.7071752369554196, sr);
        hp.highpass (38.13547087613982, 0.5, sr);

        std::vector<std::vector<double>> kw ((size_t) ch, std::vector<double> ((size_t) n));
        for (int c = 0; c < ch; ++c)
        {
            Biquad s = shelf, h = hp;   // fresh state per channel
            const auto* d = audio.getReadPointer (c);
            for (int i = 0; i < n; ++i) kw[(size_t) c][(size_t) i] = h.process (s.process ((double) d[i]));
        }

        // ── Gated integrated loudness ──────────────────────────────────────────
        const int block = (int) std::round (0.4 * sr);      // 400 ms
        const int step  = (int) std::round (0.1 * sr);      // 100 ms (75% overlap)
        std::vector<double> zBlocks;                        // per-block mean-square sum
        for (int start = 0; start + block <= n; start += step)
        {
            double z = 0.0;
            for (int c = 0; c < ch; ++c)
            {
                double ms = 0.0;
                for (int i = start; i < start + block; ++i) { const double v = kw[(size_t) c][(size_t) i]; ms += v * v; }
                z += 1.0 * (ms / block);                    // channel weight L=R=1.0
            }
            zBlocks.push_back (z);
        }
        auto loudnessOf = [] (double z) { return z > 0.0 ? -0.691 + 10.0 * std::log10 (z) : -300.0; };

        // Absolute gate at -70 LUFS.
        std::vector<double> absGated;
        for (double z : zBlocks) if (loudnessOf (z) >= -70.0) absGated.push_back (z);
        if (! absGated.empty())
        {
            double meanZ = 0.0; for (double z : absGated) meanZ += z; meanZ /= (double) absGated.size();
            const double relThresh = loudnessOf (meanZ) - 10.0;   // -10 LU relative gate
            double sum = 0.0; int cnt = 0;
            for (double z : absGated) if (loudnessOf (z) >= relThresh) { sum += z; ++cnt; }
            if (cnt > 0) r.integratedLufs = loudnessOf (sum / cnt);
        }

        // ── True peak: 4x oversample via the tested sinc reader ────────────────
        double peak = 0.0;
        const double sub[4] = { 0.0, 0.25, 0.5, 0.75 };
        for (int c = 0; c < ch; ++c)
            for (int i = 0; i < n; ++i)
                for (double s : sub)
                    peak = juce::jmax (peak, (double) std::abs (readSinc (audio, c, (double) i + s)));
        r.truePeakDb = peak > 0.0 ? 20.0 * std::log10 (peak) : -300.0;
        return r;
    }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void highpass (double fc, double Q, double sr)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * fc / sr, c = std::cos (w), al = std::sin (w) / (2 * Q);
            const double a0 = 1 + al; b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
        }
        void highShelf (double fc, double gainDb, double Q, double sr)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2.0 * juce::MathConstants<double>::pi * fc / sr, c = std::cos (w), sn = std::sin (w);
            const double al = sn / (2.0 * Q), sA = std::sqrt (A);
            const double a0 =        (A + 1) - (A - 1) * c + 2 * sA * al;
            b0 =        (A * ((A + 1) + (A - 1) * c + 2 * sA * al)) / a0;
            b1 = (-2 * A * ((A - 1) + (A + 1) * c))               / a0;
            b2 =        (A * ((A + 1) + (A - 1) * c - 2 * sA * al)) / a0;
            a1 =   (2 *   ((A - 1) - (A + 1) * c))                 / a0;
            a2 =        ((A + 1) - (A - 1) * c - 2 * sA * al)      / a0;
        }
    };
};

} // namespace stemdeck
