//  Sattari Audio Suite — formant shifting via cepstral envelope warping.
//
//  Moves the spectral ENVELOPE (vocal-tract size — perceived body/gender) independently
//  of pitch. The "different person" knob in Sattari Morph, and the per-voice character
//  in doublers/harmonisers.
//
//  Method (phase is never touched, so the excitation/pitch is preserved exactly):
//    1. windowed FFT
//    2. real cepstrum: IFFT(log|X|); low quefrency = envelope, high = harmonics
//    3. lifter away the high quefrencies, FFT back -> smooth spectral envelope
//    4. resample that envelope by the formant ratio
//    5. multiply the spectrum by (newEnvelope / oldEnvelope)
//    6. IFFT, synthesis window, overlap-add
//
//  Self-contained (embedded radix-2 FFT), pure-std, RT-safe after prepare().
#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace sattari::dsp
{

class FormantShifter
{
public:
    struct Params
    {
        float shiftSemitones = 0.0f;   // -12..+12; negative = larger/deeper
        float amount = 1.0f;           // 0 = bypass, 1 = full warp
        int   lifterQuefrency = 40;    // envelope smoothness; 30-60 for voice
        float maxGain = 8.0f;          // per-bin correction clamp
    };

    // fftOrder 10 = 1024 (~21 ms @ 48k); order 9 halves latency, coarser envelope.
    void prepare (double sampleRate, int fftOrder = 10, int overlapFactor = 4)
    {
        sr_ = sampleRate;
        order_ = std::max (8, fftOrder);
        fftSize_ = 1 << order_;
        hop_ = fftSize_ / std::max (2, overlapFactor);
        numBins_ = fftSize_ / 2 + 1;
        buildTables();

        // sqrt-Hann on BOTH analysis and synthesis (product = full Hann, COLA at 4x).
        window_.assign ((size_t) fftSize_, 0.0f);
        for (int i = 0; i < fftSize_; ++i)
            window_[(size_t) i] = std::sqrt (0.5f * (1.0f - std::cos (2.0 * kPi * i / fftSize_)));
        double acc = 0.0;
        for (int i = 0; i < fftSize_; i += hop_) acc += (double) window_[(size_t) i] * window_[(size_t) i];
        const float norm = acc > 0.0 ? (float) std::sqrt (1.0 / acc) : 1.0f;  // sqrt, not 1/acc (6 dB trap)
        for (auto& w : window_) w *= norm;

        inFifo_.assign ((size_t) fftSize_, 0.0f);
        outAcc_.assign ((size_t) fftSize_, 0.0f);
        outFifo_.assign ((size_t) hop_, 0.0f);
        re_.assign ((size_t) fftSize_, 0.0f); im_.assign ((size_t) fftSize_, 0.0f);
        cre_.assign ((size_t) fftSize_, 0.0f); cim_.assign ((size_t) fftSize_, 0.0f);
        envOld_.assign ((size_t) numBins_, 0.0f); envNew_.assign ((size_t) numBins_, 0.0f);
        pos_ = 0;
    }

    void reset()
    {
        std::fill (inFifo_.begin(), inFifo_.end(), 0.0f);
        std::fill (outAcc_.begin(), outAcc_.end(), 0.0f);
        std::fill (outFifo_.begin(), outFifo_.end(), 0.0f);
        pos_ = 0;
    }

    int latencySamples() const noexcept { return fftSize_; }

    // Safe when in == out.
    void process (const float* in, float* out, int numSamples, const Params& p)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            inFifo_[(size_t) (fftSize_ - hop_ + pos_)] = in[i];
            out[i] = outFifo_[(size_t) pos_];
            if (++pos_ >= hop_)
            {
                pos_ = 0;
                processFrame (p);
                std::memcpy (outFifo_.data(), outAcc_.data(), (size_t) hop_ * sizeof (float));
                std::memmove (outAcc_.data(), outAcc_.data() + hop_, (size_t) (fftSize_ - hop_) * sizeof (float));
                std::fill (outAcc_.end() - hop_, outAcc_.end(), 0.0f);
                std::memmove (inFifo_.data(), inFifo_.data() + hop_, (size_t) (fftSize_ - hop_) * sizeof (float));
            }
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    void processFrame (const Params& p)
    {
        for (int i = 0; i < fftSize_; ++i) { re_[(size_t) i] = inFifo_[(size_t) i] * window_[(size_t) i]; im_[(size_t) i] = 0.0f; }
        fft (re_.data(), im_.data(), false);

        for (int b = 0; b < numBins_; ++b)
        {
            const float mag = std::sqrt (re_[(size_t) b] * re_[(size_t) b] + im_[(size_t) b] * im_[(size_t) b]);
            cre_[(size_t) b] = std::log (std::max (mag, 1.0e-9f)); cim_[(size_t) b] = 0.0f;
        }
        for (int b = numBins_; b < fftSize_; ++b) { cre_[(size_t) b] = cre_[(size_t) (fftSize_ - b)]; cim_[(size_t) b] = 0.0f; }
        fft (cre_.data(), cim_.data(), true);   // cepstrum

        const int q = std::max (4, std::min (p.lifterQuefrency, fftSize_ / 4));
        for (int i = q; i <= fftSize_ - q; ++i) { cre_[(size_t) i] = 0.0f; cim_[(size_t) i] = 0.0f; }
        fft (cre_.data(), cim_.data(), false);  // log envelope
        for (int b = 0; b < numBins_; ++b) envOld_[(size_t) b] = std::exp (cre_[(size_t) b]);

        const float ratio = std::pow (2.0f, p.shiftSemitones / 12.0f);
        for (int b = 0; b < numBins_; ++b)
        {
            const float src = b / ratio;
            if (src >= numBins_ - 1)
            {
                // Continue the envelope's own top slope past Nyquist (mirroring sounds
                // metallic, a hard constant makes a shelf).
                const float top = envOld_[(size_t) (numBins_ - 1)], prev = envOld_[(size_t) (numBins_ - 9)];
                const float slope = prev > 1.0e-9f ? top / prev : 1.0f;
                const float dist = (src - (numBins_ - 1)) / 8.0f;
                envNew_[(size_t) b] = top * std::pow (std::min (slope, 1.0f), dist) * std::exp (-dist * 0.35f);
            }
            else { const int i0 = (int) src; const float fr = src - i0; envNew_[(size_t) b] = envOld_[(size_t) i0] * (1.0f - fr) + envOld_[(size_t) (i0 + 1)] * fr; }
        }

        const float lo = 1.0f / std::max (p.maxGain, 1.001f), hi = std::max (p.maxGain, 1.001f);
        for (int b = 0; b < numBins_; ++b)
        {
            float g = envNew_[(size_t) b] / std::max (envOld_[(size_t) b], 1.0e-9f);
            g = 1.0f + p.amount * (g - 1.0f);
            g = std::min (std::max (g, lo), hi);
            re_[(size_t) b] *= g; im_[(size_t) b] *= g;
            if (b > 0 && b < fftSize_ / 2) { re_[(size_t) (fftSize_ - b)] = re_[(size_t) b]; im_[(size_t) (fftSize_ - b)] = -im_[(size_t) b]; }
        }

        fft (re_.data(), im_.data(), true);
        for (int i = 0; i < fftSize_; ++i) outAcc_[(size_t) i] += re_[(size_t) i] * window_[(size_t) i];
    }

    void buildTables()
    {
        rev_.assign ((size_t) fftSize_, 0);
        for (int i = 0; i < fftSize_; ++i) { int r = 0; for (int b = 0; b < order_; ++b) if (i & (1 << b)) r |= 1 << (order_ - 1 - b); rev_[(size_t) i] = r; }
        cos_.assign ((size_t) (fftSize_ / 2), 0.0f); sin_.assign ((size_t) (fftSize_ / 2), 0.0f);
        for (int i = 0; i < fftSize_ / 2; ++i) { const double a = -2.0 * kPi * i / fftSize_; cos_[(size_t) i] = (float) std::cos (a); sin_[(size_t) i] = (float) std::sin (a); }
    }
    void fft (float* re, float* im, bool inverse) const
    {
        for (int i = 0; i < fftSize_; ++i) if (i < rev_[(size_t) i]) { std::swap (re[i], re[rev_[(size_t) i]]); std::swap (im[i], im[rev_[(size_t) i]]); }
        for (int len = 2; len <= fftSize_; len <<= 1)
        {
            const int half = len >> 1, step = fftSize_ / len;
            for (int i = 0; i < fftSize_; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const int t = j * step; const float wr = cos_[(size_t) t], wi = inverse ? -sin_[(size_t) t] : sin_[(size_t) t];
                    const int a = i + j, b = a + half;
                    const float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
                }
        }
        if (inverse) { const float s = 1.0f / fftSize_; for (int i = 0; i < fftSize_; ++i) { re[i] *= s; im[i] *= s; } }
    }

    double sr_ = 48000.0;
    int order_ = 10, fftSize_ = 1024, hop_ = 256, numBins_ = 513, pos_ = 0;
    std::vector<float> window_, inFifo_, outAcc_, outFifo_, re_, im_, cre_, cim_, envOld_, envNew_;
    std::vector<float> cos_, sin_; std::vector<int> rev_;
};

} // namespace sattari::dsp
