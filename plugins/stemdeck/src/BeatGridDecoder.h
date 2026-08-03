#pragma once

// Tempo-state beat decoder used after the neural model's frame activations.
//
// Each state represents "a beat at frame t with period p".  The Viterbi path
// balances neural activation (emission probability), a global tempo prior, and
// gradual period transitions.  Unlike threshold peak-picking, the state path
// remains defined through quiet intros and breakdowns; unlike a fixed-period
// grid, it can follow genuine tempo drift without jumping at individual peaks.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace stemdeck
{
struct BeatGridDecoder
{
    struct Result
    {
        std::vector<int> frames;
        double bpm = 0.0;
        double confidence = 0.0;
        int referencePeriodFrames = 0;

        explicit operator bool() const noexcept { return frames.size() >= 4 && bpm > 0.0; }
    };

    static Result decode (const std::vector<float>& activation,
                          double framesPerSecond,
                          double minimumBpm = 60.0,
                          double maximumBpm = 200.0)
    {
        Result result;
        const int frameCount = (int) activation.size();
        if (frameCount < 16 || framesPerSecond <= 0.0
            || minimumBpm <= 0.0 || maximumBpm <= minimumBpm)
            return result;

        auto sorted = activation;
        std::sort (sorted.begin(), sorted.end());
        const double median = sorted[sorted.size() / 2];
        const double upper = sorted[sorted.size() * 9 / 10];
        const double scale = std::max (0.04, upper - median);
        if ((double) sorted.back() - median < 0.015)
            return result; // no evidence: do not manufacture a grid from silence

        const int minPeriod = std::max (1, (int) std::floor (framesPerSecond * 60.0 / maximumBpm));
        const int maxPeriod = std::max (minPeriod + 1,
                                        (int) std::ceil (framesPerSecond * 60.0 / minimumBpm));
        const int periodCount = maxPeriod - minPeriod + 1;

        // Normalised autocorrelation supplies a broad global prior.  Subtracting
        // the median keeps quiet backgrounds from winning merely by duration.
        int referencePeriod = 0;
        double bestCorrelation = -std::numeric_limits<double>::infinity();
        for (int period = minPeriod; period <= maxPeriod; ++period)
        {
            double cross = 0.0, leftEnergy = 0.0, rightEnergy = 0.0;
            for (int frame = period; frame < frameCount; ++frame)
            {
                const double a = std::max (0.0, (double) activation[(size_t) frame] - median);
                const double b = std::max (0.0, (double) activation[(size_t) (frame - period)] - median);
                cross += a * b;
                leftEnergy += a * a;
                rightEnergy += b * b;
            }
            const double normalised = cross / std::sqrt (std::max (1.0e-12, leftEnergy * rightEnergy));
            const double bpm = 60.0 * framesPerSecond / (double) period;
            const double tieBreak = 0.015 * std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 1.5, 2.0));
            if (normalised + tieBreak > bestCorrelation)
            {
                bestCorrelation = normalised + tieBreak;
                referencePeriod = period;
            }
        }
        if (referencePeriod <= 0)
            return result;

        const auto index = [periodCount, minPeriod] (int frame, int period)
        {
            return (size_t) frame * (size_t) periodCount + (size_t) (period - minPeriod);
        };
        const size_t stateCount = (size_t) frameCount * (size_t) periodCount;
        constexpr float unreachable = -std::numeric_limits<float>::infinity();
        std::vector<float> scores (stateCount, unreachable);
        std::vector<short> previousPeriods (stateCount, (short) -1);

        const auto emission = [&] (int frame)
        {
            const double z = ((double) activation[(size_t) frame] - median) / scale;
            // Positive activations pull beats toward transients. A small negative
            // floor lets the timing model cross a breakdown instead of ending it.
            return (float) (1.8 * std::clamp (z, -0.35, 1.5));
        };

        for (int frame = 0; frame < frameCount; ++frame)
        {
            for (int period = minPeriod; period <= maxPeriod; ++period)
            {
                const size_t here = index (frame, period);
                const double globalDelta = (double) (period - referencePeriod)
                                         / (double) std::max (1, referencePeriod);
                const float globalPenalty = (float) (0.32 * globalDelta * globalDelta);

                // A path must begin near the track edge. That is what preserves a
                // musically useful grid through an activation-free ambient intro.
                if (frame < maxPeriod)
                    scores[here] = emission (frame) - globalPenalty;

                const int previousFrame = frame - period;
                if (previousFrame < 0)
                    continue;

                const int transitionRadius = std::max (2, (int) std::ceil (period * 0.12));
                const int lo = std::max (minPeriod, period - transitionRadius);
                const int hi = std::min (maxPeriod, period + transitionRadius);
                float best = unreachable;
                int bestPreviousPeriod = -1;
                for (int previousPeriod = lo; previousPeriod <= hi; ++previousPeriod)
                {
                    const float previous = scores[index (previousFrame, previousPeriod)];
                    if (! std::isfinite (previous))
                        continue;
                    const double delta = (double) (period - previousPeriod)
                                       / (double) std::max (1, previousPeriod);
                    const float candidate = previous - (float) (12.0 * delta * delta);
                    if (candidate > best)
                    {
                        best = candidate;
                        bestPreviousPeriod = previousPeriod;
                    }
                }
                if (bestPreviousPeriod >= 0)
                {
                    const float candidate = best + emission (frame) - globalPenalty;
                    if (candidate > scores[here])
                    {
                        scores[here] = candidate;
                        previousPeriods[here] = (short) bestPreviousPeriod;
                    }
                }
            }
        }

        // Require the path to reach the final beat-sized window. It may end just
        // before trailing ambience, but cannot select only an energetic excerpt.
        float bestTerminalScore = unreachable;
        int terminalFrame = -1, terminalPeriod = -1;
        const int terminalStart = std::max (0, frameCount - maxPeriod);
        for (int frame = terminalStart; frame < frameCount; ++frame)
            for (int period = minPeriod; period <= maxPeriod; ++period)
                if (scores[index (frame, period)] > bestTerminalScore)
                {
                    bestTerminalScore = scores[index (frame, period)];
                    terminalFrame = frame;
                    terminalPeriod = period;
                }
        if (terminalFrame < 0)
            return result;

        std::vector<int> reverseFrames;
        std::vector<int> reversePeriods;
        int frame = terminalFrame, period = terminalPeriod;
        while (frame >= 0 && period >= minPeriod && period <= maxPeriod)
        {
            reverseFrames.push_back (frame);
            reversePeriods.push_back (period);
            const int previousPeriod = (int) previousPeriods[index (frame, period)];
            if (previousPeriod < 0)
                break;
            frame -= period;
            period = previousPeriod;
        }
        std::reverse (reverseFrames.begin(), reverseFrames.end());
        std::reverse (reversePeriods.begin(), reversePeriods.end());
        if (reverseFrames.size() < 4)
            return result;

        result.frames = std::move (reverseFrames);
        result.referencePeriodFrames = referencePeriod;

        std::vector<int> intervals;
        intervals.reserve (result.frames.size() - 1);
        double selectedStrength = 0.0;
        for (size_t i = 0; i < result.frames.size(); ++i)
        {
            selectedStrength += std::max (0.0, ((double) activation[(size_t) result.frames[i]] - median) / scale);
            if (i > 0)
                intervals.push_back (result.frames[i] - result.frames[i - 1]);
        }
        std::sort (intervals.begin(), intervals.end());
        const double medianPeriod = (double) intervals[intervals.size() / 2];
        result.bpm = medianPeriod > 0.0 ? 60.0 * framesPerSecond / medianPeriod : 0.0;

        double timingError = 0.0;
        for (size_t i = 1; i < result.frames.size(); ++i)
        {
            const double expected = (double) reversePeriods[i];
            timingError += std::abs ((double) (result.frames[i] - result.frames[i - 1]) - expected)
                         / std::max (1.0, expected);
        }
        const double regularity = 1.0 / (1.0 + timingError / (double) intervals.size());
        const double evidence = selectedStrength / (double) result.frames.size();
        // Map arbitrarily strong evidence asymptotically instead of hard-clamping
        // it to 1.0. A generated click can be extremely clear, but the decoder
        // should never claim mathematical certainty about musical time.
        result.confidence = std::clamp (
            (1.0 - std::exp (-evidence / 0.85)) * regularity,
            0.0, 0.999);
        return result;
    }
};
} // namespace stemdeck
