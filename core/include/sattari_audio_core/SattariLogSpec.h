//  Sattari Audio Suite — pseudo-CQT log-frequency spectrogram.
//
//  The key-detection feature the SPECTROGRAM key model consumes:
//      FFT magnitude -> 84 semitone-spaced triangular filters (C1 + b semitones)
//      -> dB (ref = max) -> per-track standardise -> time-pool to 256 frames.
//
//  Deliberately NOT a real constant-Q transform: a multi-rate CQT can't be
//  reproduced bit-for-bit against librosa, and any drift between training and
//  inference costs accuracy. This is just an FFT plus a filterbank built from a
//  closed form, so scripts/pcqt_featurize.py (training) and this header
//  (inference) agree by construction.
//
//  Audio is resampled to kSampleRate first: the model was trained at 22.05 kHz and
//  the lowest filters (C1 = 32.7 Hz, neighbours ~2 Hz apart) span less than one FFT
//  bin at 48 kHz, so running at the host rate would quietly distort the low end.
#pragma once

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace sattari::dsp
{

class LogSpec
{
public:
    static constexpr double kSampleRate = 22050.0;
    static constexpr int    kFftSize    = 8192;
    static constexpr int    kHop        = 4096;
    static constexpr int    kBins       = 84;      // C1 .. B7, one per semitone
    static constexpr int    kFrames     = 256;     // time-pooled width
    static constexpr double kFMin       = 32.703195662574829;   // C1

    LogSpec() { buildFilterbank(); buildWindow(); }

    // mono @ any sample rate -> [kBins * kFrames] (row-major, bin-major) in `out`.
    // Returns false if the input is too short / silent to featurise.
    bool compute (const float* mono, int numSamples, double sampleRate, std::vector<float>& out) const
    {
        std::vector<float> y;
        resampleTo22k (mono, numSamples, sampleRate, y);
        if ((int) y.size() < kFftSize) y.resize ((size_t) kFftSize, 0.0f);

        juce::dsp::FFT fft ((int) std::log2 ((double) kFftSize));
        std::vector<float> scratch ((size_t) kFftSize * 2, 0.0f);
        std::vector<std::vector<float>> frames;
        const int n = (int) y.size();
        for (int s = 0; s + kFftSize <= n; s += kHop)
        {
            std::fill (scratch.begin(), scratch.end(), 0.0f);
            for (int i = 0; i < kFftSize; ++i) scratch[(size_t) i] = y[(size_t) (s + i)] * window[(size_t) i];
            fft.performFrequencyOnlyForwardTransform (scratch.data());
            std::vector<float> band ((size_t) kBins, 0.0f);
            for (int b = 0; b < kBins; ++b)
            {
                const auto& f = fb[(size_t) b];
                double acc = 0.0;
                for (size_t k = 0; k < f.idx.size(); ++k)
                    acc += (double) f.w[k] * (double) scratch[(size_t) f.idx[k]];
                band[(size_t) b] = (float) acc;
            }
            frames.push_back (std::move (band));
        }
        if (frames.empty()) return false;

        // dB (ref = max), then standardise over the whole track.
        const int T = (int) frames.size();
        const size_t Tsz = (size_t) T;
        std::vector<float> M ((size_t) kBins * Tsz);
        float mx = -1.0e30f;
        for (int t = 0; t < T; ++t)
            for (int b = 0; b < kBins; ++b)
            {
                const float v = 20.0f * std::log10 (std::max (frames[(size_t) t][(size_t) b], 1.0e-10f));
                M[(size_t) b * Tsz + (size_t) t] = v; mx = std::max (mx, v);
            }
        double sum = 0.0;
        for (auto& v : M) { v -= mx; sum += v; }
        const double mean = sum / M.size();
        double var = 0.0;
        for (auto v : M) var += (v - mean) * (v - mean);
        const double sd = std::sqrt (var / M.size());
        for (auto& v : M) v = (float) ((v - mean) / (sd + 1.0e-6));

        // Time-pool to kFrames by averaging over linspace edges (matches numpy).
        out.assign ((size_t) kBins * kFrames, 0.0f);
        for (int f = 0; f < kFrames; ++f)
        {
            const int e0 = (int) ((double) f       / kFrames * T);
            const int e1 = std::max (e0 + 1, (int) ((double) (f + 1) / kFrames * T));
            const int hi = std::min (e1, T);
            for (int b = 0; b < kBins; ++b)
            {
                double acc = 0.0; int cnt = 0;
                for (int t = e0; t < hi; ++t) { acc += M[(size_t) b * Tsz + (size_t) t]; ++cnt; }
                out[(size_t) b * (size_t) kFrames + (size_t) f] = (float) (cnt ? acc / cnt : 0.0);
            }
        }
        return true;
    }

private:
    struct Filter { std::vector<int> idx; std::vector<float> w; };

    void buildWindow()
    {
        window.resize ((size_t) kFftSize);
        for (int i = 0; i < kFftSize; ++i)   // symmetric Hann == np.hanning
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / (kFftSize - 1)));
    }

    void buildFilterbank()
    {
        const int nFreq = kFftSize / 2 + 1;
        std::vector<double> freqs ((size_t) nFreq);
        for (int k = 0; k < nFreq; ++k) freqs[(size_t) k] = (double) k * kSampleRate / kFftSize;
        fb.resize ((size_t) kBins);
        for (int b = 0; b < kBins; ++b)
        {
            const double lo = kFMin * std::pow (2.0, (double) b / 12.0);
            const double c  = kFMin * std::pow (2.0, (double) (b + 1) / 12.0);
            const double hi = kFMin * std::pow (2.0, (double) (b + 2) / 12.0);
            Filter f; double area = 0.0;
            for (int k = 0; k < nFreq; ++k)
            {
                const double fr = freqs[(size_t) k];
                double w = 0.0;
                if (fr > lo && fr <= c)      w = (fr - lo) / std::max (c - lo, 1.0e-9);
                else if (fr > c && fr < hi)  w = (hi - fr) / std::max (hi - c, 1.0e-9);
                if (w > 0.0) { f.idx.push_back (k); f.w.push_back ((float) w); area += w; }
            }
            if (area > 0.0) for (auto& w : f.w) w = (float) (w / area);   // area-normalise
            fb[(size_t) b] = std::move (f);
        }
    }

    // Linear resample to 22.05 kHz. Offline-only path (Key Map scan), so plain
    // interpolation is fine and keeps the feature deterministic across hosts.
    static void resampleTo22k (const float* in, int n, double sr, std::vector<float>& out)
    {
        if (n <= 0) { out.clear(); return; }
        if (std::abs (sr - kSampleRate) < 1.0e-6) { out.assign (in, in + n); return; }
        const double ratio = sr / kSampleRate;
        const int m = (int) ((double) n / ratio);
        out.assign ((size_t) std::max (0, m), 0.0f);
        for (int i = 0; i < m; ++i)
        {
            const double x = i * ratio;
            const int i0 = (int) x, i1 = std::min (n - 1, i0 + 1);
            const double fr = x - i0;
            out[(size_t) i] = (float) ((1.0 - fr) * in[i0] + fr * in[i1]);
        }
    }

    std::vector<float>  window;
    std::vector<Filter> fb;
};

} // namespace sattari::dsp
