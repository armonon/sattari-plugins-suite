#pragma once

// =============================================================================
//  Sattari Audio Suite — HPCP-style chromagram (pure std, no JUCE/FFT)
// -----------------------------------------------------------------------------
//  A "harmonic pitch-class profile" chroma that actually works on real, full-mix
//  music — the thing the naive linear-FFT histogram in SattariChromagram.h does
//  NOT do. `accumulateChroma()` there folds every FFT bin's raw power into the
//  nearest of 12 classes; on real tracks the low-frequency bins are too coarse
//  (a 4096-pt FFT at 44.1 kHz has ~10.8 Hz bins — a whole semitone near the bass),
//  timbre/loudness dominate, and any tuning drift from A440 smears energy across
//  neighbouring classes. That is why the full-mix detector sits near chance.
//
//  This header ADDS a separate accumulator (it does not touch accumulateChroma,
//  which song-key / auto-pitch / brain depend on). It layers the standard tricks
//  that move MIR chroma from ~chance to usable (Gómez 2006 HPCP; Fujishima 1999):
//
//    1. SPECTRAL PEAKS only  — local maxima above a floor, not every bin, so
//       broadband/noise energy doesn't wash out the harmony.
//    2. PARABOLIC INTERPOLATION — each peak's TRUE frequency from a 3-point fit,
//       so a note is placed to a fractional pitch class, fixing the coarse-bin
//       problem far better than snapping raw bins to the nearest semitone.
//    3. SPECTRAL WHITENING — divide each peak by a smoothed spectral envelope, so
//       a bright timbre or a loud section doesn't dominate the profile shape.
//    4. HARMONIC SUMMATION — every peak also credits the fundamentals it could be
//       a harmonic of (f/2, f/3 … f/8) with a 0.6^(h-1) decay, so a note's own
//       pitch class is reinforced by its overtones (the core HPCP idea).
//    5. RAISED-COSINE SPREAD — each contribution is smeared over neighbouring
//       high-resolution bins, so tiny frequency errors don't hard-quantise.
//    6. TUNING CORRECTION — the global deviation from A440 is estimated from the
//       circular mean of every peak's fractional-semitone position, then the fold
//       to 12 classes is shifted by it, so an out-of-tune track still lands right.
//    7. BAND LIMITING / MID EMPHASIS — restrict to ~[55 Hz .. 5 kHz] and weight
//       the mid octaves (where harmony lives) over sub-bass/kick and hiss.
//    8. NORMALISE the final 12-bin vector.
//
//  Usage mirrors accumulateChroma's per-frame flow, but state lives in the class
//  because tuning is global: build one HpcpChromagram per track, feed every FFT
//  frame's magnitude spectrum, then finish() into a 12-bin chroma for the shared
//  SattariKeyDetector. Pure std so it stays as widely includable as the original.
// =============================================================================

#include <algorithm>
#include <cmath>
#include <vector>

namespace sattari::dsp
{

// -----------------------------------------------------------------------------
//  HPCP chromagram accumulator. One instance per track / analysis window.
// -----------------------------------------------------------------------------
class HpcpChromagram
{
public:
    // High-resolution folded octave: kRes sub-bins per semitone (12 * kRes total).
    // 3 sub-bins/semitone (~0.33 st) + the raised-cosine spread + linear-interp
    // fold gives smooth placement without an oversized table.
    static constexpr int    kRes            = 3;
    static constexpr int    kHrBins         = 12 * kRes;    // 36
    static constexpr int    kNumHarmonics   = 8;            // f, f/2 … f/8
    static constexpr double kHarmonicDecay  = 0.6;          // weight 0.6^(h-1)
    static constexpr double kSpreadHalfSt   = 2.0 / 3.0;    // raised-cosine ± semitones
    static constexpr double kPeakFloorRel   = 0.005;        // peak must exceed this * maxMag
    static constexpr double kWhitenSmooth   = 0.85;         // spectral-envelope 1-pole coeff

    HpcpChromagram (double minHz = 55.0, double maxHz = 5000.0) noexcept
        : minHz_ (minHz), maxHz_ (maxHz) {}

    void reset() noexcept
    {
        std::fill (hr_.begin(), hr_.end(), 0.0);
        tuneCos_ = tuneSin_ = 0.0;
    }

    // Fold one FFT magnitude frame into the running HPCP state.
    // `magnitude` has `numBins` entries (typically fftSize/2 + 1); binHz = fs/fftSize.
    void accumulate (const float* magnitude, int numBins, double binHz) noexcept
    {
        if (binHz <= 0.0 || numBins < 4) return;

        // Band-limit to the musical range before doing anything else.
        const int firstBin = std::max (1, (int) std::ceil (minHz_ / binHz));
        const int lastBin  = std::min (numBins - 2, (int) std::floor (maxHz_ / binHz));
        if (lastBin - firstBin < 2) return;

        // --- spectral whitening envelope: forward+backward 1-pole smoothing of
        //     the magnitude, so peaks are scored by prominence over local timbre. ---
        env_.assign ((size_t) numBins, 0.0);
        double e = 0.0;
        for (int b = 0; b < numBins; ++b)
        { e = kWhitenSmooth * e + (1.0 - kWhitenSmooth) * (double) magnitude[b]; env_[(size_t) b] = e; }
        e = 0.0;
        for (int b = numBins - 1; b >= 0; --b)
        { e = kWhitenSmooth * e + (1.0 - kWhitenSmooth) * (double) magnitude[b];
          env_[(size_t) b] = 0.5 * (env_[(size_t) b] + e); }

        // Peak floor relative to the loudest in-band bin.
        double maxMag = 0.0;
        for (int b = firstBin; b <= lastBin; ++b) maxMag = std::max (maxMag, (double) magnitude[b]);
        if (maxMag <= 0.0) return;
        const double floor = kPeakFloorRel * maxMag;

        for (int b = firstBin; b <= lastBin; ++b)
        {
            const double m  = (double) magnitude[b];
            const double mL = (double) magnitude[b - 1];
            const double mR = (double) magnitude[b + 1];
            // local maximum above the floor
            if (! (m > mL && m >= mR && m > floor)) continue;

            // parabolic interpolation -> true (fractional) bin, then frequency
            const double denom = mL - 2.0 * m + mR;
            double delta = 0.0;
            if (std::fabs (denom) > 1.0e-12)
            {
                delta = 0.5 * (mL - mR) / denom;
                if (delta > 0.5 || delta < -0.5) delta = 0.0;   // reject bad fits
            }
            const double freq = ((double) b + delta) * binHz;
            if (freq <= 0.0) continue;

            // prominence over the smoothed envelope = spectral-whitened weight
            const double env    = env_[(size_t) b] + 1.0e-9;
            const double promin = m / env;
            const double bw     = bandWeight (freq);
            const double w      = promin * bw;
            if (w <= 0.0) continue;

            // --- tuning: circular accumulation of this peak's semitone fraction ---
            const double midi = 69.0 + 12.0 * std::log2 (freq / 440.0);
            const double ang  = 2.0 * M_PI * midi;            // period = 1 semitone
            tuneCos_ += w * std::cos (ang);
            tuneSin_ += w * std::sin (ang);

            // --- harmonic summation: credit the fundamentals f/h that would put a
            //     harmonic here, reinforcing this note's own pitch class. ---
            double hw = 1.0;
            for (int h = 1; h <= kNumHarmonics; ++h, hw *= kHarmonicDecay)
            {
                const double fFund = freq / (double) h;
                const double midiF = 69.0 + 12.0 * std::log2 (fFund / 440.0);
                double pc = std::fmod (midiF, 12.0);
                if (pc < 0.0) pc += 12.0;                     // [0,12)
                spread (pc, w * hw);
            }
        }
    }

    // Estimated global tuning offset in semitones, in [-0.5, 0.5]. Positive = the
    // track sits sharp of A440. Meaningful only after some peaks were accumulated.
    double tuningOffsetSemitones() const noexcept
    {
        if (tuneCos_ == 0.0 && tuneSin_ == 0.0) return 0.0;
        return std::atan2 (tuneSin_, tuneCos_) / (2.0 * M_PI);
    }

    // Collapse the high-resolution octave into a tuning-corrected, normalised
    // 12-bin chroma (sum = 1). Non-destructive; the accumulator can keep running.
    void finish (double out12[12]) const noexcept
    {
        std::fill (out12, out12 + 12, 0.0);
        const double offset = tuningOffsetSemitones();   // semitones sharp of A440

        for (int j = 0; j < kHrBins; ++j)
        {
            const double energy = hr_[(size_t) j];
            if (energy <= 0.0) continue;
            // sub-bin -> continuous semitone, correct for tuning, split across the
            // two nearest pitch classes (linear interp keeps the shift smooth).
            double pos = (double) j / (double) kRes - offset;
            pos = std::fmod (pos, 12.0);
            if (pos < 0.0) pos += 12.0;
            const int    lo   = (int) std::floor (pos);
            const double frac = pos - (double) lo;
            out12[lo % 12]       += energy * (1.0 - frac);
            out12[(lo + 1) % 12] += energy * frac;
        }

        double total = 0.0;
        for (int i = 0; i < 12; ++i) total += out12[i];
        if (total > 1.0e-12)
            for (int i = 0; i < 12; ++i) out12[i] /= total;
    }

private:
    // Raised-cosine spread of `weight` around continuous pitch class `pc` (in
    // semitones, [0,12)) into the wrapped high-resolution histogram.
    void spread (double pc, double weight) noexcept
    {
        const int span = (int) std::ceil (kSpreadHalfSt * kRes);
        const double centreBin = pc * (double) kRes;
        const int    c0 = (int) std::round (centreBin);
        for (int d = -span; d <= span; ++d)
        {
            const int    idx  = ((c0 + d) % kHrBins + kHrBins) % kHrBins;
            const double binPc = (double) (c0 + d) / (double) kRes;
            double dist = std::fabs (binPc - pc);
            dist = std::min (dist, 12.0 - dist);             // wrap distance
            if (dist > kSpreadHalfSt) continue;
            const double win = 0.5 * (1.0 + std::cos (M_PI * dist / kSpreadHalfSt));
            hr_[(size_t) idx] += weight * win;
        }
    }

    // Mid-octave emphasis: log-Gaussian bump peaking ~440 Hz, gently rolling off
    // sub-bass/kick and high hiss so harmony-bearing mids dominate the profile.
    static double bandWeight (double freq) noexcept
    {
        const double centre = 440.0;     // A4, middle of the harmony band
        const double sigma  = 2.2;       // octaves (broad — this only de-emphasises extremes)
        const double octaves = std::log2 (freq / centre);
        return std::exp (-0.5 * (octaves / sigma) * (octaves / sigma));
    }

    double minHz_, maxHz_;
    std::array<double, kHrBins> hr_ {};
    std::vector<double>         env_;
    double tuneCos_ = 0.0, tuneSin_ = 0.0;
};

} // namespace sattari::dsp
