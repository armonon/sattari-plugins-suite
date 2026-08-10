#pragma once

#include <sattari_audio_core/SattariFft.h>
#include <sattari_audio_core/SattariSliceTable.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// =============================================================================
//  SattariOnsetCurve — offline onset detection for Sattari Chop.
// -----------------------------------------------------------------------------
//  Chop's sensitivity slider must re-slice LIVE while the user drags (spec §2.2):
//  markers appear/disappear against the waveform and they stop where it looks right.
//  That only works if the expensive part runs ONCE.
//
//  So this splits in two:
//    analyze()     — one pass over the whole source: STFT, and per hop a detection
//                    function combining spectral flux + high-frequency content +
//                    amplitude-envelope derivative. Stores the curve.
//    candidates()  — O(n) re-threshold of the stored curve. Cheap enough per drag
//                    frame. No FFT is re-run.
//
//  NOTE: this is NOT VoxAnalyzer. VoxAnalyzer is a real-time streaming note-on
//  detector with an internal adaptive threshold and no retrievable curve; it cannot
//  give §2.2's behaviour. The DSP ideas are the same; the shape is different.
//
//  Pure-std. analyze() allocates (worker thread, never audio); candidates() allocates
//  only its result. Both are deterministic — same input, same markers.
// =============================================================================

namespace sattari::dsp
{

class OnsetCurve
{
public:
    struct Config
    {
        int fftOrder = 10;            // 1024-pt window
        int hop      = 128;           // spec §2.1: 128-sample onset resolution
        float wFlux  = 1.0f;          // broadband attacks
        float wHfc   = 0.6f;          // fricatives / hats
        float wAmp   = 0.5f;          // soft onsets the flux misses
        float medianWindowSec = 0.2f; // adaptive threshold window (spec: last 200 ms)
        float refractoryMs    = 30.0f;// suppress double-triggers on one transient
    };

    // One pass over mono source audio. Everything after this is cheap.
    // (Config{} cannot be a default ARGUMENT here — the nested class's default member
    //  initialisers aren't complete at that point — but it is fine inside a body.)
    void analyze (const float* mono, std::int64_t numSamples, double sampleRate)
    {
        analyze (mono, numSamples, sampleRate, Config{});
    }

    void analyze (const float* mono, std::int64_t numSamples, double sampleRate, const Config& cfg)
    {
        cfg_ = cfg;
        sr_  = sampleRate > 0.0 ? sampleRate : 44100.0;
        hop_ = cfg.hop > 0 ? cfg.hop : 128;

        fft_.prepare (cfg.fftOrder);
        const int n = fft_.size();
        const int bins = fft_.numBins();

        window_.assign ((std::size_t) n, 0.0f);
        for (int i = 0; i < n; ++i)   // Hann
            window_[(std::size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * Fft::kPi * i / (n - 1));

        frame_.assign ((std::size_t) n, 0.0f);
        mag_.assign ((std::size_t) bins, 0.0f);
        prevMag_.assign ((std::size_t) bins, 0.0f);

        curve_.clear();
        hopPos_.clear();
        if (mono == nullptr || numSamples <= 0) return;

        const std::int64_t numHops = (numSamples - n) > 0 ? ((numSamples - n) / hop_ + 1) : 1;
        curve_.reserve ((std::size_t) numHops);
        hopPos_.reserve ((std::size_t) numHops);

        float prevRms = 0.0f;
        bool first = true;

        for (std::int64_t h = 0; h < numHops; ++h)
        {
            const std::int64_t start = h * hop_;

            float rms = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const std::int64_t idx = start + i;
                const float s = (idx < numSamples) ? mono[idx] : 0.0f;
                frame_[(std::size_t) i] = s * window_[(std::size_t) i];
                rms += s * s;
            }
            rms = std::sqrt (rms / (float) n);

            fft_.magnitude (frame_.data(), mag_.data());

            // Spectral flux: sum of POSITIVE bin deltas (energy arriving, not leaving).
            // HFC: frequency-weighted energy, which spikes on bright transients.
            float flux = 0.0f, hfc = 0.0f, magSum = 0.0f;
            for (int b = 0; b < bins; ++b)
            {
                const float m = mag_[(std::size_t) b];
                const float d = m - prevMag_[(std::size_t) b];
                if (d > 0.0f) flux += d;
                hfc += m * (float) b;
                magSum += m;
                prevMag_[(std::size_t) b] = m;
            }
            hfc = magSum > 1.0e-9f ? hfc / (magSum * (float) bins) : 0.0f;   // normalised centroid

            const float ampDeriv = first ? 0.0f : std::max (0.0f, rms - prevRms);
            prevRms = rms;
            first = false;

            curve_.push_back (cfg_.wFlux * flux + cfg_.wHfc * hfc + cfg_.wAmp * ampDeriv * 20.0f);
            hopPos_.push_back (start + n / 2);   // centre of the analysis window
        }

        smoothCurve();      // hop resolution is ~2.9 ms; raw ODF noise creates fake peaks
        normaliseCurve();
    }

    // The stored detection function, normalised to 0..1 (for drawing under the waveform).
    const std::vector<float>& curve() const noexcept { return curve_; }
    std::int64_t hopPosition (std::size_t i) const noexcept
    {
        return i < hopPos_.size() ? hopPos_[i] : 0;
    }
    bool analysed() const noexcept { return ! curve_.empty(); }

    // Re-threshold the stored curve. sensitivity 0..1: higher = more markers.
    // O(n) over the curve — safe to call on every drag frame.
    std::vector<SliceTable::Candidate> candidates (float sensitivity) const
    {
        std::vector<SliceTable::Candidate> out;
        if (curve_.empty()) return out;

        const float s = sensitivity < 0.0f ? 0.0f : (sensitivity > 1.0f ? 1.0f : sensitivity);
        // Multiplier over the LOCAL median. It must stay > 1 at every sensitivity: on dense
        // material the median is already high, so a k below 1 fires on more than half the
        // hops (measured: 21-27 markers/s on real tracks). Range 2.6 (sparse) -> 1.08 (dense).
        const float k = 2.6f - 1.52f * s;
        const int win = std::max (4, (int) (cfg_.medianWindowSec * sr_ / (double) hop_));
        const std::int64_t refractory = (std::int64_t) (cfg_.refractoryMs * 0.001 * sr_);

        std::vector<float> scratch;
        scratch.reserve ((std::size_t) win);

        // NB: do not seed lastPos with INT64_MIN — (pos - lastPos) would overflow (UB) and
        // the refractory test would reject every candidate.
        std::int64_t lastPos = 0;
        bool havePrev = false;
        for (std::size_t i = 0; i < curve_.size(); ++i)
        {
            const float v = curve_[i];
            if (v <= 0.0f) continue;

            // Must be the strongest hop in its neighbourhood, not merely up-then-down.
            // A +/-1 hop test on a 2.9 ms grid fires on ODF ripple; real onsets dominate
            // a window comparable to the refractory period.
            if (! isLocalMax (i, kPeakNeighbourhood)) continue;

            const float thr = k * localMedian (i, win, scratch) + 0.02f;
            if (v < thr) continue;

            const std::int64_t pos = hopPos_[i];
            if (havePrev && pos - lastPos < refractory) continue;
            lastPos = pos;
            havePrev = true;
            out.push_back ({ pos, v });
        }
        return out;
    }

private:
    // Peak must dominate +/- this many hops (~8 hops = ~23 ms at hop 128 / 44.1k).
    static constexpr int kPeakNeighbourhood = 8;
    // Short moving average over the ODF. Removes hop-scale ripple that otherwise reads as
    // dozens of "local peaks" per second on dense material, without blurring real attacks.
    static constexpr int kSmoothHalf = 2;

    void smoothCurve()
    {
        if (curve_.size() < (std::size_t) (2 * kSmoothHalf + 1)) return;
        std::vector<float> out (curve_.size(), 0.0f);
        for (std::size_t i = 0; i < curve_.size(); ++i)
        {
            const std::size_t lo = i >= (std::size_t) kSmoothHalf ? i - (std::size_t) kSmoothHalf : 0;
            const std::size_t hi = std::min (curve_.size() - 1, i + (std::size_t) kSmoothHalf);
            float sum = 0.0f;
            for (std::size_t j = lo; j <= hi; ++j) sum += curve_[j];
            out[i] = sum / (float) (hi - lo + 1);
        }
        curve_.swap (out);
    }

    bool isLocalMax (std::size_t i, int half) const noexcept
    {
        const float v = curve_[i];
        const std::size_t lo = i >= (std::size_t) half ? i - (std::size_t) half : 0;
        const std::size_t hi = std::min (curve_.size() - 1, i + (std::size_t) half);
        for (std::size_t j = lo; j <= hi; ++j)
        {
            if (j == i) continue;
            if (curve_[j] > v) return false;
            // Exact stored-value tie: keep the earliest hop only. memcmp expresses
            // the intentional bit identity without triggering unsafe float-equality
            // diagnostics under the suite's strict warning profile.
            if (std::memcmp (&curve_[j], &v, sizeof (float)) == 0 && j < i) return false;
        }
        return true;
    }

    void normaliseCurve()
    {
        float peak = 0.0f;
        for (float v : curve_) peak = std::max (peak, v);
        if (peak > 1.0e-9f) for (float& v : curve_) v /= peak;
    }

    // Median of the curve over the `win` hops ending at i (the adaptive floor).
    float localMedian (std::size_t i, int win, std::vector<float>& scratch) const
    {
        const std::size_t lo = i >= (std::size_t) win ? i - (std::size_t) win : 0;
        scratch.clear();
        for (std::size_t j = lo; j <= i; ++j) scratch.push_back (curve_[j]);
        if (scratch.empty()) return 0.0f;
        const std::size_t mid = scratch.size() / 2;
        std::nth_element (scratch.begin(), scratch.begin() + (std::ptrdiff_t) mid, scratch.end());
        return scratch[mid];
    }

    Config cfg_ {};
    double sr_ = 44100.0;
    int    hop_ = 128;

    Fft fft_;
    std::vector<float> window_, frame_, mag_, prevMag_;
    std::vector<float> curve_;
    std::vector<std::int64_t> hopPos_;
};

} // namespace sattari::dsp
