#pragma once

#include <cmath>
#include <cstddef>
#include <utility>   // std::swap
#include <vector>

// =============================================================================
//  SattariFft — a small, shared radix-2 FFT.
// -----------------------------------------------------------------------------
//  The suite grew five separate embedded FFTs (VoxAnalyzer, Chromagram, HpcpChroma,
//  LogSpec, FormantShifter). This is the shared one, introduced with SattariOnsetCurve
//  so new cores stop adding a sixth; the existing five can migrate onto it later.
//
//  Pure-std. All allocation happens in prepare(); forward()/magnitude() do none, so it
//  is safe to call from a worker thread (and from audio if prepared ahead of time).
// =============================================================================

namespace sattari::dsp
{

class Fft
{
public:
    // Local pi: M_PI is POSIX, not ISO C++, so don't depend on it leaking out of <cmath>.
    static constexpr double kPi = 3.14159265358979323846;

    // N = 1 << order. Precomputes the bit-reversal table and twiddles.
    void prepare (int order)
    {
        order_ = order < 1 ? 1 : order;
        n_ = 1 << order_;

        rev_.assign ((std::size_t) n_, 0);
        for (int i = 0; i < n_; ++i)
        {
            int r = 0;
            for (int b = 0; b < order_; ++b) if (i & (1 << b)) r |= 1 << (order_ - 1 - b);
            rev_[(std::size_t) i] = r;
        }

        // Twiddles per stage, flattened: stage s uses half = 1<<s entries.
        cos_.assign ((std::size_t) n_, 0.0f);
        sin_.assign ((std::size_t) n_, 0.0f);
        for (int s = 0, base = 0; s < order_; ++s)
        {
            const int half = 1 << s;
            for (int k = 0; k < half; ++k)
            {
                const double ang = -kPi * (double) k / (double) half;
                cos_[(std::size_t) (base + k)] = (float) std::cos (ang);
                sin_[(std::size_t) (base + k)] = (float) std::sin (ang);
            }
            base += half;
        }

        re_.assign ((std::size_t) n_, 0.0f);
        im_.assign ((std::size_t) n_, 0.0f);
    }

    int  size() const noexcept { return n_; }
    int  numBins() const noexcept { return n_ / 2 + 1; }
    bool ready() const noexcept { return n_ > 0; }

    // In-place complex forward FFT over re[0..N) / im[0..N).
    void forward (float* re, float* im) const noexcept
    {
        if (n_ <= 0) return;

        for (int i = 0; i < n_; ++i)
        {
            const int r = rev_[(std::size_t) i];
            if (r > i) { std::swap (re[i], re[r]); std::swap (im[i], im[r]); }
        }

        for (int s = 0, base = 0; s < order_; ++s)
        {
            const int half = 1 << s;
            const int step = half << 1;
            for (int i = 0; i < n_; i += step)
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cos_[(std::size_t) (base + k)];
                    const float wi = sin_[(std::size_t) (base + k)];
                    const int a = i + k, b = i + k + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
            base += half;
        }
    }

    // Magnitude spectrum of a real frame of exactly size() samples.
    // magOut receives numBins() values. Uses internal scratch (no allocation).
    void magnitude (const float* timeDomain, float* magOut) const noexcept
    {
        if (n_ <= 0) return;
        for (int i = 0; i < n_; ++i) { re_[(std::size_t) i] = timeDomain[i]; im_[(std::size_t) i] = 0.0f; }
        forward (re_.data(), im_.data());
        const int bins = numBins();
        for (int i = 0; i < bins; ++i)
        {
            const float r = re_[(std::size_t) i], m = im_[(std::size_t) i];
            magOut[i] = std::sqrt (r * r + m * m);
        }
    }

private:
    int order_ = 0, n_ = 0;
    std::vector<int>   rev_;
    std::vector<float> cos_, sin_;
    mutable std::vector<float> re_, im_;
};

} // namespace sattari::dsp
