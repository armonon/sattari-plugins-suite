#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sattari::dsp
{
// Causal harmonic/percussive soft mask (HPSS, after Fitzgerald 2010) for real-time use.
//
// Fed a stream of STFT magnitude frames, it estimates for each bin how TONAL (harmonic)
// vs how NOISY/percussive it is and returns a soft "tonalness" weight in [0,1]:
//   ~1  steady partial (a real note/harmonic)  -> safe to pitch-shift
//   ~0  broadband / transient (a hat, air, a click) -> leave it exactly where it is
//
// Harmonic energy is HORIZONTAL structure in the spectrogram (steady over TIME at one
// frequency): estimated by a median over the most recent magnitude frames at each bin.
// Percussive energy is VERTICAL structure (broadband within ONE frame): estimated by a
// median over a frequency window around each bin. The soft mask is the Wiener form
//     w = H^p / (H^p + P^p).
//
// The time median uses only PAST frames (causal), so it adds NO latency — at the cost of
// a one-sided estimate, which is the right trade for a live plugin. A light temporal EMA
// on the output weight suppresses frame-to-frame flicker (zipper) at routing thresholds.
// After prepare() there is no allocation and no locking: real-time safe.
class HarmonicMask
{
public:
    // numBins   : spectrum length fed to process() (e.g. FFT/2 + 1).
    // timeFrames: history depth for the harmonic (time) median; forced odd.
    // freqRadius: half-width of the percussive (frequency) median window.
    void prepare (int numBins, int timeFrames = 15, int freqRadius = 15)
    {
        bins  = std::max (1, numBins);
        nTime = std::max (1, timeFrames | 1);   // odd so the median is a real sample
        fRad  = std::max (1, freqRadius);
        ring.assign ((std::size_t) bins * (std::size_t) nTime, 0.0f);
        tcol.assign ((std::size_t) nTime, 0.0f);
        fwin.assign ((std::size_t) (2 * fRad + 1), 0.0f);
        smooth.assign ((std::size_t) bins, 0.5f);
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        std::fill (smooth.begin(), smooth.end(), 0.5f);
        writeIdx = 0;
        filled   = 0;
    }

    // Push the current magnitude frame (length numBins) and write the per-bin tonalness
    // weight into wOut (length numBins). p = mask sharpness (2 = power Wiener; higher =
    // harder split). ema = temporal smoothing of the weight in [0,1] (0 = none).
    void process (const float* mag, float* wOut, float p = 2.0f, float ema = 0.5f)
    {
        // Store the current frame into the ring.
        float* row = &ring[(std::size_t) writeIdx * (std::size_t) bins];
        for (int k = 0; k < bins; ++k) row[(std::size_t) k] = mag[k];
        writeIdx = (writeIdx + 1) % nTime;
        if (filled < nTime) ++filled;

        const float a = std::min (std::max (ema, 0.0f), 1.0f);

        for (int k = 0; k < bins; ++k)
        {
            // Harmonic estimate: median over the last `filled` frames at bin k.
            for (int t = 0; t < filled; ++t)
                tcol[(std::size_t) t] = ring[(std::size_t) t * (std::size_t) bins + (std::size_t) k];
            const float H = medianOf (tcol.data(), filled);

            // Percussive estimate: median over the frequency window around bin k (current frame).
            int cnt = 0;
            for (int j = -fRad; j <= fRad; ++j)
            {
                const int kk = k + j;
                if (kk < 0 || kk >= bins) continue;
                fwin[(std::size_t) cnt++] = mag[kk];
            }
            const float P = medianOf (fwin.data(), cnt);

            const float hp = std::pow (H, p), pp = std::pow (P, p);
            const float denom = hp + pp;
            const float w = denom > 1.0e-20f ? hp / denom : 0.5f;

            // Temporal EMA on the weight for stable routing.
            const float s = a * smooth[(std::size_t) k] + (1.0f - a) * w;
            smooth[(std::size_t) k] = s;
            wOut[k] = s;
        }
    }

    int numBins() const { return bins; }

private:
    static float medianOf (float* buf, int n)
    {
        if (n <= 0) return 0.0f;
        const int mid = n / 2;
        std::nth_element (buf, buf + mid, buf + n);
        return buf[(std::size_t) mid];
    }

    int bins = 0, nTime = 1, fRad = 1, writeIdx = 0, filled = 0;
    std::vector<float> ring;               // nTime x bins, ring of recent magnitude frames
    std::vector<float> tcol, fwin, smooth; // scratch + per-bin smoothed weight
};
} // namespace sattari::dsp
