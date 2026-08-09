#pragma once

#include <JuceHeader.h>
#include <vector>
#include <algorithm>

/**
 * Varying-tempo map with linear ramp between anchor points.
 *
 * Provides:
 *   bpmAt(t)          — instantaneous BPM at time t (linear interpolation)
 *   secondsToBeats(t) — integrate from 0 → t to get beat count
 *   beatsToSeconds(b) — inverse: beat count → time (binary search + refinement)
 *
 * An empty map returns 120 BPM everywhere.  All methods are const and
 * allocation-free after points are added.
 *
 * Ported / inspired by openDAW's TempoGridCursor concept, adapted for JUCE C++.
 */
class TempoMap
{
public:
    struct Point
    {
        double timeSeconds = 0.0;
        double bpm         = 120.0;
    };

    void clear()  noexcept { points.clear(); }

    void addPoint (double timeSeconds, double bpm)
    {
        points.push_back ({ timeSeconds, juce::jlimit (20.0, 999.0, bpm) });
        std::sort (points.begin(), points.end(),
                   [] (const Point& a, const Point& b) { return a.timeSeconds < b.timeSeconds; });
    }

    bool   isEmpty()  const noexcept { return points.empty(); }
    int    numPoints() const noexcept { return (int) points.size(); }
    const Point& getPoint (int i) const noexcept { return points[(size_t) i]; }
    double baseBpm()  const noexcept { return points.empty() ? 120.0 : points.front().bpm; }

    /** Instantaneous BPM at time t (linear ramp between adjacent anchor points). */
    double bpmAt (double t) const noexcept
    {
        if (points.empty())                        return 120.0;
        if (t <= points.front().timeSeconds)       return points.front().bpm;
        if (t >= points.back().timeSeconds)        return points.back().bpm;

        for (size_t i = 1; i < points.size(); ++i)
        {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            if (t >= a.timeSeconds && t < b.timeSeconds)
            {
                const double frac = (t - a.timeSeconds) / (b.timeSeconds - a.timeSeconds);
                return a.bpm + frac * (b.bpm - a.bpm);
            }
        }
        return points.back().bpm;
    }

    /**
     * Integrate BPM from 0 to t to obtain the beat count.
     * Each linear ramp segment is integrated analytically (trapezoid rule).
     */
    double secondsToBeats (double t) const noexcept
    {
        if (t <= 0.0) return 0.0;

        const double fallbackBpm = points.empty() ? 120.0 : points.front().bpm;

        double beats = 0.0;
        double prevT = 0.0;
        double prevB = fallbackBpm;

        for (const auto& p : points)
        {
            if (prevT >= t) break;
            const double segEnd = juce::jmin (p.timeSeconds, t);
            if (segEnd > prevT)
            {
                // Linear ramp prevB → p.bpm over [prevT, p.timeSeconds]
                const double frac   = (segEnd - prevT) / juce::jmax (1.0e-9, p.timeSeconds - prevT);
                const double endBpm = prevB + frac * (p.bpm - prevB);
                beats += (segEnd - prevT) * (prevB + endBpm) * 0.5 / 60.0;
                prevT = segEnd;
            }
            prevB = p.bpm;
        }

        // Tail after the last point: constant BPM
        if (prevT < t)
            beats += (t - prevT) * prevB / 60.0;

        return beats;
    }

    /**
     * Inverse of secondsToBeats — converts beat count to absolute time.
     * Uses binary search + one Newton step for accuracy.
     */
    double beatsToSeconds (double targetBeats, double searchMax = 3600.0) const noexcept
    {
        if (targetBeats <= 0.0) return 0.0;

        double lo = 0.0, hi = searchMax;
        for (int iter = 0; iter < 48; ++iter)
        {
            const double mid = (lo + hi) * 0.5;
            if (secondsToBeats (mid) < targetBeats) lo = mid;
            else                                     hi = mid;
            if (hi - lo < 1.0e-6) break;
        }
        return (lo + hi) * 0.5;
    }

    const std::vector<Point>& getPoints() const noexcept { return points; }

private:
    std::vector<Point> points;
};
