#pragma once

// =============================================================================
//  Sattari TempoEstimator — global tempo from a spectral-flux/onset envelope
// -----------------------------------------------------------------------------
//  The caller owns the front-end and supplies one non-negative onset-strength
//  sample per analysis hop. This class handles the musical inference: local-floor
//  removal, normalised autocorrelation, harmonic-lag reinforcement, octave
//  selection, sub-hop interpolation and a winner-vs-alternative confidence.
//
//  Pure std and deterministic. It allocates and is intended for an analysis
//  worker, never the audio callback.
// =============================================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sattari::dsp
{

struct TempoResult
{
    double bpm = 0.0;
    double confidence = 0.0;
    bool valid = false;
    bool octaveAmbiguous = false;
};

class TempoEstimator
{
public:
    static TempoResult analyse (const std::vector<float>& onset,
                                double envelopeRateHz,
                                double minBpm = 55.0,
                                double maxBpm = 205.0)
    {
        TempoResult result;
        if (envelopeRateHz <= 0.0 || minBpm <= 0.0 || maxBpm <= minBpm
            || onset.size() < (std::size_t) std::ceil (envelopeRateHz * 6.0))
            return result;

        // Live callers cap their own rolling history; file callers intentionally
        // provide the complete track so every section contributes.
        const std::size_t begin = 0;
        const std::size_t n = onset.size() - begin;

        std::vector<double> prefix (n + 1, 0.0);
        for (std::size_t i = 0; i < n; ++i)
            prefix[i + 1] = prefix[i] + std::max (0.0, (double) onset[begin + i]);

        // Subtract a one-second trailing floor, half-wave rectify and compress.
        // This retains attacks while preventing a loud sustained section from
        // creating a large zero-lag bias.
        const std::size_t floorFrames = std::max<std::size_t> (
            2, (std::size_t) std::round (envelopeRateHz));
        std::vector<double> x (n, 0.0);
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::size_t lo = i > floorFrames ? i - floorFrames : 0;
            const double floor = (prefix[i + 1] - prefix[lo])
                / (double) (i + 1 - lo);
            const double novelty = std::max (0.0, (double) onset[begin + i] - 0.72 * floor);
            x[i] = std::sqrt (novelty);
        }

        double mean = 0.0;
        for (double v : x) mean += v;
        mean /= (double) n;
        double energy = 0.0;
        for (double& v : x) { v -= mean; energy += v * v; }
        if (energy <= 1.0e-12) return result;

        const int minLag = std::max (2, (int) std::floor (
            envelopeRateHz * 60.0 / maxBpm));
        const int maxLag = std::min ((int) n / 3, (int) std::ceil (
            envelopeRateHz * 60.0 / minBpm));
        if (maxLag <= minLag + 2) return result;

        std::vector<double> correlation ((std::size_t) maxLag * 3 + 2, 0.0);
        const int correlationMaxLag = (int) correlation.size() - 1;
        for (int lag = 1; lag <= correlationMaxLag && lag < (int) n; ++lag)
        {
            double cross = 0.0, left = 0.0, right = 0.0;
            for (std::size_t i = (std::size_t) lag; i < n; ++i)
            {
                const double a = x[i];
                const double b = x[i - (std::size_t) lag];
                cross += a * b;
                left += a * a;
                right += b * b;
            }
            const double denom = std::sqrt (left * right);
            correlation[(std::size_t) lag] = denom > 1.0e-12 ? cross / denom : 0.0;
        }

        auto harmonicScore = [&] (int lag)
        {
            double score = correlation[(std::size_t) lag];
            if (2 * lag <= correlationMaxLag)
                score += 0.42 * std::max (0.0, correlation[(std::size_t) (2 * lag)]);
            if (3 * lag <= correlationMaxLag)
                score += 0.20 * std::max (0.0, correlation[(std::size_t) (3 * lag)]);
            // Very small prior: resolve genuinely equal half/double candidates
            // toward the conventional song-tempo band without overriding data.
            const double bpm = envelopeRateHz * 60.0 / (double) lag;
            const double octaves = std::abs (std::log2 (bpm / 120.0));
            return score * (1.0 - 0.025 * std::min (2.0, octaves));
        };

        int bestLag = minLag;
        double bestScore = -2.0;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            const double score = harmonicScore (lag);
            if (score > bestScore) { bestScore = score; bestLag = lag; }
        }

        // Coarse onset rates can align a slower metrical layer more perfectly
        // than the actual beat (174 BPM may align at 87; a three-beat phrase at
        // 80 can conceal a 120 BPM tactus). Review the common x2 and x3/2 fast
        // layers, but promote only when the signal has a substantial peak at
        // that shorter lag. Genuine slow pulse trains have no such peak and
        // therefore remain slow.
        if (envelopeRateHz * 60.0 / (double) bestLag < 92.0)
        {
            const int slowLag = bestLag;
            const double slowScore = bestScore;
            const double slowCorrelation = correlation[(std::size_t) slowLag];
            int promotedLag = bestLag;
            double promotedScore = bestScore;
            double promotedPreference = -2.0;
            for (double ratio : { 2.0, 1.5 })
            {
                int fastLag = (int) std::round ((double) slowLag / ratio);
                double fastScore = -2.0;
                for (int lag = std::max (minLag, fastLag - 1);
                     lag <= std::min (maxLag, fastLag + 1); ++lag)
                {
                    const double score = harmonicScore (lag);
                    if (score > fastScore) { fastScore = score; fastLag = lag; }
                }
                const double fastBpm = envelopeRateHz * 60.0 / (double) fastLag;
                if (fastBpm < 95.0 || fastBpm > 190.0
                    || correlation[(std::size_t) fastLag] < 0.40 * slowCorrelation
                    || fastScore < 0.45 * slowScore)
                    continue;
                const double preference = fastScore * (
                    1.0 - 0.06 * std::min (2.0,
                        std::abs (std::log2 (fastBpm / 120.0))));
                if (preference > promotedPreference)
                {
                    promotedPreference = preference;
                    promotedLag = fastLag;
                    promotedScore = fastScore;
                }
            }
            bestLag = promotedLag;
            bestScore = promotedScore;
        }
        if (bestScore <= 0.08 || correlation[(std::size_t) bestLag] <= 0.04)
            return result;

        // Sub-hop interpolation around the chosen autocorrelation peak.
        double fractionalLag = (double) bestLag;
        if (bestLag > 1 && bestLag < correlationMaxLag)
        {
            const double a = correlation[(std::size_t) (bestLag - 1)];
            const double b = correlation[(std::size_t) bestLag];
            const double c = correlation[(std::size_t) (bestLag + 1)];
            const double den = a - 2.0 * b + c;
            if (std::abs (den) > 1.0e-9)
                fractionalLag += std::clamp (0.5 * (a - c) / den, -0.5, 0.5);
        }

        // Find a genuinely different contender (not the same broadened peak).
        double secondScore = -2.0;
        int secondLag = bestLag;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            if (std::abs (lag - bestLag) <= 1) continue;
            const double score = harmonicScore (lag);
            if (score > secondScore) { secondScore = score; secondLag = lag; }
        }

        result.bpm = envelopeRateHz * 60.0 / fractionalLag;
        const double separation = std::clamp (
            (bestScore - std::max (0.0, secondScore))
                / std::max (1.0e-9, std::abs (bestScore)),
            0.0, 1.0);
        result.confidence = std::clamp (
            0.65 * separation + 0.35 * correlation[(std::size_t) bestLag],
            0.0, 1.0);
        const double octaveRatio = secondLag > 0
            ? std::max ((double) secondLag / (double) bestLag,
                        (double) bestLag / (double) secondLag)
            : 0.0;
        result.octaveAmbiguous = secondScore > 0.0
            && std::abs (octaveRatio - 2.0) < 0.18
            && secondScore >= bestScore * 0.88;
        result.valid = std::isfinite (result.bpm)
            && result.bpm >= minBpm - 1.0 && result.bpm <= maxBpm + 1.0
            && result.confidence >= 0.05;
        if (! result.valid) result.bpm = 0.0;
        return result;
    }
};

} // namespace sattari::dsp
