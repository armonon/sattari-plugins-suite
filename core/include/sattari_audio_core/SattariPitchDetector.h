#pragma once

// =============================================================================
//  Sattari Audio Suite — shared monophonic pitch detector
// -----------------------------------------------------------------------------
//  One YIN-based fundamental tracker for the whole suite, replacing the
//  near-identical copies that lived in Sub Conjurer, Royal Chain (Tune), and
//  others. Intentionally pure-std (no JUCE) so it is unit-testable on its own and
//  cheap to include anywhere. (Upgraded from normalised autocorrelation to YIN /
//  cumulative-mean-normalised difference for robust tracking of noisy material;
//  this is real YIN, not the offline pYIN HMM and not CREPE.)
//
//  Usage (per voice/channel that needs pitch):
//     sattari::dsp::PitchDetector det;
//     det.prepare (sampleRate, 50.0f, 1000.0f);          // in prepareToPlay
//     det.pushBlock (monoSamples, numSamples);           // every processBlock
//     auto r = det.analyse();                            // on your own schedule
//     if (r.voiced) useFrequency (r.frequencyHz);
//
//  The detector owns its analysis ring; callers just push mono samples and ask
//  for a result when they want one (analysis is O(window * lags), so gate it on
//  a hop counter rather than calling every sample).
// =============================================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sattari::dsp
{

// --- Small pitch/scale helpers (also duplicated across plugins) --------------
inline float midiToHz (float midiNote) noexcept
{
    return 440.0f * std::pow (2.0f, (midiNote - 69.0f) / 12.0f);
}

inline float hzToMidi (float hz) noexcept
{
    return hz > 0.0f ? 69.0f + 12.0f * std::log2 (hz / 440.0f) : -1.0f;
}

inline int nearestMidiNote (float hz) noexcept
{
    return hz > 0.0f ? (int) std::lround (hzToMidi (hz)) : -1;
}

// Snap a frequency to the nearest equal-tempered semitone (chromatic).
inline float snapHzToSemitone (float hz) noexcept
{
    return hz > 0.0f ? midiToHz ((float) std::round (hzToMidi (hz))) : hz;
}

// Fold an octave-erroring estimate toward a running reference, suppressing the
// classic autocorrelation half/double-pitch jumps (a candidate more than ~a
// fifth away gets octave-shifted back toward the reference).
inline float foldToOctaveNear (float candidateHz, float referenceHz) noexcept
{
    if (candidateHz <= 0.0f || referenceHz <= 0.0f)
        return candidateHz;
    while (candidateHz > referenceHz * 1.6f)   candidateHz *= 0.5f;
    while (candidateHz < referenceHz * 0.625f) candidateHz *= 2.0f;
    return candidateHz;
}

// --- Shared scale-snap (replaces per-plugin tables in voxkey/arp/sub) ---------
// Covers the union of scales used across the suite. ChromaticOff = no quantize.
enum class ScaleId
{
    ChromaticOff = 0, Major, Minor, PentaMaj, PentaMin, Dorian, Mixolydian, HarmMinor
};

// Pitch-class intervals (0-11) for each ScaleId; ChromaticOff has none.
inline const int* scaleIntervals (ScaleId id, int& outCount) noexcept
{
    static constexpr int kMajor[]       = { 0,2,4,5,7,9,11 };
    static constexpr int kMinor[]       = { 0,2,3,5,7,8,10 };
    static constexpr int kPentaMaj[]    = { 0,2,4,7,9 };
    static constexpr int kPentaMin[]    = { 0,3,5,7,10 };
    static constexpr int kDorian[]      = { 0,2,3,5,7,9,10 };
    static constexpr int kMixolydian[]  = { 0,2,4,5,7,9,10 };
    static constexpr int kHarmMinor[]   = { 0,2,3,5,7,8,11 };

    switch (id)
    {
        case ScaleId::Major:       outCount = 7; return kMajor;
        case ScaleId::Minor:       outCount = 7; return kMinor;
        case ScaleId::PentaMaj:    outCount = 5; return kPentaMaj;
        case ScaleId::PentaMin:    outCount = 5; return kPentaMin;
        case ScaleId::Dorian:      outCount = 7; return kDorian;
        case ScaleId::Mixolydian:  outCount = 7; return kMixolydian;
        case ScaleId::HarmMinor:   outCount = 7; return kHarmMinor;
        case ScaleId::ChromaticOff:
        default:                   outCount = 0; return nullptr;
    }
}

// Snap a MIDI note (0-127) to the nearest pitch-class in the chosen scale rooted
// at `root` (0=C..11=B). ChromaticOff returns the note unchanged.
// Ties round DOWN: the lower of two equidistant scale tones is chosen, matching
// the standard pitch-correction convention (felt as more "in the key").
inline int snapMidiToScale (int midiNote, int root, ScaleId id) noexcept
{
    if (id == ScaleId::ChromaticOff || midiNote < 0 || midiNote > 127)
        return midiNote;

    int n = 0;
    const int* intervals = scaleIntervals (id, n);
    if (intervals == nullptr || n <= 0)
        return midiNote;

    const int pc = ((midiNote - root) % 12 + 12) % 12;
    int bestDelta = 128;
    for (int i = 0; i < n; ++i)
    {
        int d = intervals[i] - pc;
        if (d > 6)  d -= 12;
        if (d < -6) d += 12;
        if (std::abs (d) < std::abs (bestDelta)
            || (std::abs (d) == std::abs (bestDelta) && d < bestDelta))
            bestDelta = d;
    }
    const int out = midiNote + bestDelta;
    return out < 0 ? 0 : (out > 127 ? 127 : out);
}

class PitchDetector
{
public:
    struct Result
    {
        float frequencyHz = 0.0f;
        float confidence  = 0.0f;   // periodicity = 1 - YIN aperiodicity [0, 1]
        float rms         = 0.0f;
        bool  voiced      = false;  // true when confident + above the noise floor
    };

    // windowSeconds should cover at least one period of the lowest frequency.
    void prepare (double sampleRate, float minHz = 50.0f, float maxHz = 1000.0f,
                  float windowSeconds = 0.045f)
    {
        sr   = sampleRate > 0.0 ? sampleRate : 44100.0;
        loHz = minHz > 1.0f ? minHz : 1.0f;
        hiHz = maxHz > loHz ? maxHz : loHz + 1.0f;
        len  = clampPow2 ((int) (sr * (double) windowSeconds), 1024, 16384);
        ring.assign ((size_t) len, 0.0f);
        work.assign ((size_t) len, 0.0f);
        scores.assign ((size_t) len, 0.0f);
        pos = 0;
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        pos = 0;
    }

    int windowLength() const noexcept { return len; }

    void pushBlock (const float* mono, int numSamples)
    {
        if (len <= 0)
            return;
        for (int i = 0; i < numSamples; ++i)
        {
            ring[(size_t) pos] = mono[i];
            pos = (pos + 1) % len;
        }
    }

    Result analyse (float confidenceThreshold = 0.3f)
    {
        Result r;
        if (len <= 0)
            return r;

        // Linearise the ring (oldest -> newest).
        for (int i = 0; i < len; ++i)
            work[(size_t) i] = ring[(size_t) ((pos + i) % len)];

        double energy = 0.0;
        for (int i = 0; i < len; ++i)
            energy += (double) work[(size_t) i] * (double) work[(size_t) i];
        r.rms = (float) std::sqrt (energy / (double) len);
        if (r.rms < 1.0e-4f)
            return r;

        const int minLag = std::max (1, (int) (sr / (double) hiHz));
        const int maxLag = std::min (len - 2, (int) (sr / (double) loHz));
        if (maxLag <= minLag)
            return r;

        // YIN (de Cheveigne & Kawahara 2002): cumulative-mean-normalised
        // difference function. This tracks weakly-pitched / noisy material far
        // better than normalised autocorrelation - the CMNDF divides out
        // amplitude and the zero-lag bias that makes autocorrelation octave-jump.
        // `scores` holds d'(tau). Honest scope: real YIN, not the offline pYIN
        // HMM and not CREPE.
        scores[0] = 1.0f;
        double runningSum = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double d = 0.0;
            const int limit = len - tau;
            for (int i = 0; i < limit; ++i)
            {
                const double diff = (double) work[(size_t) i] - (double) work[(size_t) (i + tau)];
                d += diff * diff;
            }
            runningSum += d;
            scores[(size_t) tau] = (float) ((runningSum > 0.0) ? (d * (double) tau / runningSum) : 1.0);
        }

        // Absolute threshold: take the FIRST dip below threshold, stepped to its
        // local minimum - the shortest qualifying period, which defeats
        // sub-harmonic / octave-down errors by construction.
        constexpr float yinThreshold = 0.15f;
        int bestLag = -1;
        for (int tau = minLag; tau < maxLag; ++tau)
        {
            if (scores[(size_t) tau] < yinThreshold)
            {
                while (tau + 1 < maxLag && scores[(size_t) (tau + 1)] < scores[(size_t) tau])
                    ++tau;
                bestLag = tau;
                break;
            }
        }
        // No dip cleared the threshold: global CMNDF minimum (lower confidence).
        if (bestLag < 0)
        {
            float mn = 2.0f;
            for (int tau = minLag; tau <= maxLag; ++tau)
                if (scores[(size_t) tau] < mn) { mn = scores[(size_t) tau]; bestLag = tau; }
        }
        if (bestLag < minLag) bestLag = minLag;

        // Periodicity = 1 - aperiodicity (the CMNDF value at the dip).
        const float periodicity = std::min (1.0f, std::max (0.0f, 1.0f - scores[(size_t) bestLag]));
        if (periodicity < confidenceThreshold)
        {
            r.confidence = periodicity;
            return r;
        }

        // Parabolic interpolation around the chosen lag for sub-sample accuracy.
        double refinedLag = bestLag;
        if (bestLag > minLag && bestLag < maxLag)
        {
            const float s0 = scores[(size_t) (bestLag - 1)];
            const float s1 = scores[(size_t) bestLag];
            const float s2 = scores[(size_t) (bestLag + 1)];
            const float denom = s0 - 2.0f * s1 + s2;
            if (std::abs (denom) > 1.0e-9f)
                refinedLag += 0.5 * (double) (s0 - s2) / (double) denom;
        }

        r.confidence = periodicity;
        r.frequencyHz = (float) (sr / refinedLag);
        r.voiced = true;
        return r;
    }

private:
    static int clampPow2 (int wanted, int lo, int hi)
    {
        int p = 1;
        while (p < wanted) p <<= 1;
        return std::min (std::max (p, lo), hi);
    }

    double sr = 44100.0;
    float loHz = 50.0f, hiHz = 1000.0f;
    int len = 0, pos = 0;
    std::vector<float> ring, work, scores;
};

} // namespace sattari::dsp
