#pragma once
// ContinuousMotionEstimator — derives pitch velocity from raw YIN frequencyHz
// rather than from integer/float-rounded MIDI values.  Pure C++17, no JUCE.
//
// Key invariant: every frame that enters push() carries the sub-sample-precise
// frequency from YIN parabolic interpolation.  Converting to MIDI in double
// (not float) and estimating velocity with regression over a window eliminates
// the ~20–30 MIDI/sec jitter that plagued the per-hop integer MIDI path.

#include <algorithm>
#include <array>
#include <cmath>

struct ContinuousMotionEstimator
{
    static constexpr int kCapacity = 64;   // ~372 ms at 256/44100 blocks

    double freqHist[kCapacity] {};
    double midiHist[kCapacity] {};   // 69 + 12·log2(f/440), full double
    double timeHist[kCapacity] {};   // cumulative seconds since last reset
    int    histPos  = 0;
    int    histFill = 0;
    double cumTime  = 0.0;

    // Per-block analysis output — written by analyse(), readable by caller.
    struct MotionFrame
    {
        double midiFloat           = 0.0;
        double centPosition        = 0.0;   // cents from A4=0
        double velocityMidiPerSec  = 0.0;
        double velocityCentsPerSec = 0.0;
        double direction           = 0.0;   // +1 up / -1 down / 0 still
        double confidence          = 0.0;   // 0..1, agreement between estimators
        double acceleration        = 0.0;   // MIDI/sec²
        bool   isVibrato           = false;
    };

    enum class Estimator
    {
        FirstDerivative,      // (m[n-1] - m[n-2]) / dt — fastest, noisiest
        MultiHopAverage,      // mean of all adjacent-pair slopes in window
        LeastSquares,         // OLS linear regression slope
        RobustMedian,         // adjacent-pair Theil-Sen approximation
        ConfidenceWeighted,   // blend LSQ + Median by agreement
    };

    void reset()
    {
        histPos = histFill = 0;
        cumTime = 0.0;
        for (int i = 0; i < kCapacity; ++i)
            freqHist[i] = midiHist[i] = timeHist[i] = 0.0;
    }

    // Call once per voiced block.  frequencyHz must be the raw YIN output.
    void push (double frequencyHz, double blockSec)
    {
        if (frequencyHz <= 0.0 || blockSec <= 0.0) return;
        cumTime += blockSec;
        const double mf = 69.0 + 12.0 * std::log2 (frequencyHz / 440.0);
        freqHist[histPos] = frequencyHz;
        midiHist[histPos] = mf;
        timeHist[histPos] = cumTime;
        histPos = (histPos + 1) % kCapacity;
        if (histFill < kCapacity) ++histFill;
    }

    // Fill outMidi/outTime (oldest → newest) with frames whose cumTime is
    // within the last windowSec seconds.  Returns number of frames filled.
    // Stack-safe: caller provides the arrays; kCapacity-sized buffers suffice.
    int readWindow (double windowSec,
                    double* outMidi,
                    double* outTime,
                    int     maxOut) const
    {
        if (histFill == 0 || maxOut <= 0) return 0;
        const int    last = (histPos - 1 + kCapacity) % kCapacity;
        const double tNow = timeHist[last];
        const double tCut = tNow - windowSec;

        double tmpM[kCapacity], tmpT[kCapacity];
        int n = 0;
        for (int i = 0; i < histFill && n < maxOut; ++i)
        {
            const int idx = (histPos - 1 - i + kCapacity * 2) % kCapacity;
            if (timeHist[idx] < tCut) break;
            tmpM[n] = midiHist[idx];
            tmpT[n] = timeHist[idx];
            ++n;
        }
        // Reverse newest-first → oldest-first
        for (int l = 0, r = n - 1; l < r; ++l, --r)
        {
            const double tm = tmpM[l]; tmpM[l] = tmpM[r]; tmpM[r] = tm;
            const double tt = tmpT[l]; tmpT[l] = tmpT[r]; tmpT[r] = tt;
        }
        for (int i = 0; i < n; ++i) { outMidi[i] = tmpM[i]; outTime[i] = tmpT[i]; }
        return n;
    }

    // -------------------------------------------------------------------------
    // Estimator variants — static, operate on caller-provided arrays.
    // -------------------------------------------------------------------------

    // 1. Single frame-to-frame derivative (fastest response, highest noise)
    static double firstDerivative (const double* m, const double* t, int n)
    {
        if (n < 2) return 0.0;
        const double dt = t[n - 1] - t[n - 2];
        return dt > 1e-12 ? (m[n - 1] - m[n - 2]) / dt : 0.0;
    }

    // 2. Mean of all adjacent-pair slopes in window
    static double multiHopAverage (const double* m, const double* t, int n)
    {
        if (n < 2) return 0.0;
        double sum = 0.0; int cnt = 0;
        for (int i = 1; i < n; ++i)
        {
            const double dt = t[i] - t[i - 1];
            if (dt > 1e-12) { sum += (m[i] - m[i - 1]) / dt; ++cnt; }
        }
        return cnt > 0 ? sum / cnt : 0.0;
    }

    // 3. OLS linear regression slope (minimises RMSE for clean linear motion)
    static double leastSquaresSlope (const double* m, const double* t, int n)
    {
        if (n < 2) return 0.0;
        double sumX = 0, sumY = 0, sumXX = 0, sumXY = 0;
        for (int i = 0; i < n; ++i)
        {
            const double x = t[i] - t[0];
            sumX  += x;  sumY  += m[i];
            sumXX += x * x;  sumXY += x * m[i];
        }
        const double denom = (double) n * sumXX - sumX * sumX;
        return std::abs (denom) > 1e-15
                   ? ((double) n * sumXY - sumX * sumY) / denom
                   : 0.0;
    }

    // 4. Adjacent-pair Theil-Sen (median of adjacent slopes — robust to outliers)
    static double robustMedianSlope (const double* m, const double* t, int n)
    {
        if (n < 2) return 0.0;
        double slopes[kCapacity];
        int ns = 0;
        for (int i = 1; i < n && ns < kCapacity; ++i)
        {
            const double dt = t[i] - t[i - 1];
            if (dt > 1e-12) slopes[ns++] = (m[i] - m[i - 1]) / dt;
        }
        if (ns == 0) return 0.0;
        std::sort (slopes, slopes + ns);
        return (ns % 2) ? slopes[ns / 2]
                        : 0.5 * (slopes[ns / 2 - 1] + slopes[ns / 2]);
    }

    // 5. Confidence-weighted blend: LSQ dominates when estimators agree,
    //    median takes over when they diverge (high-jitter frames).
    static double confidenceWeightedSlope (const double* m, const double* t, int n)
    {
        if (n < 2) return 0.0;
        const double ls  = leastSquaresSlope  (m, t, n);
        const double med = robustMedianSlope   (m, t, n);
        const double diff = std::abs (ls - med);
        const double wLsq = diff < 2.0 ? 0.70 : 0.30;
        return wLsq * ls + (1.0 - wLsq) * med;
    }

    // -------------------------------------------------------------------------
    // estimate — run a single estimator over a windowSec window
    // -------------------------------------------------------------------------
    double estimateVelocity (double windowSec = 0.020,
                             Estimator est    = Estimator::FirstDerivative) const
    {
        double m[kCapacity], t[kCapacity];
        const int n = readWindow (windowSec, m, t, kCapacity);
        switch (est)
        {
            case Estimator::FirstDerivative:    return firstDerivative    (m, t, n);
            case Estimator::MultiHopAverage:    return multiHopAverage    (m, t, n);
            case Estimator::LeastSquares:       return leastSquaresSlope  (m, t, n);
            case Estimator::RobustMedian:       return robustMedianSlope  (m, t, n);
            case Estimator::ConfidenceWeighted: return confidenceWeightedSlope (m, t, n);
        }
        return 0.0;
    }

    // -------------------------------------------------------------------------
    // analyse — full MotionFrame using the chosen estimator + window.
    // Defaults reflect the MotionBench Phase 3 winner: FirstDerivative @ 20ms
    // achieved best overall RMSE (1.788 MIDI/sec) on deterministic trajectories.
    // ConfidenceWeighted at wider windows may be preferred for noisy real-YIN
    // input — re-run the bench after IntentBench Phase 5 to confirm.
    // -------------------------------------------------------------------------
    MotionFrame analyse (double    windowSec = 0.020,
                         Estimator est       = Estimator::FirstDerivative) const
    {
        MotionFrame out;
        if (histFill < 2) return out;

        const int last = (histPos - 1 + kCapacity) % kCapacity;
        out.midiFloat    = midiHist[last];
        out.centPosition = (out.midiFloat - 69.0) * 100.0;

        double m[kCapacity], t[kCapacity];
        const int n = readWindow (windowSec, m, t, kCapacity);
        if (n < 2) return out;

        // Always compute both for confidence scoring
        const double ls  = leastSquaresSlope  (m, t, n);
        const double med = robustMedianSlope   (m, t, n);
        const double diff = std::abs (ls - med);

        switch (est)
        {
            case Estimator::FirstDerivative:
                out.velocityMidiPerSec = firstDerivative (m, t, n); break;
            case Estimator::MultiHopAverage:
                out.velocityMidiPerSec = multiHopAverage (m, t, n); break;
            case Estimator::LeastSquares:
                out.velocityMidiPerSec = ls; break;
            case Estimator::RobustMedian:
                out.velocityMidiPerSec = med; break;
            case Estimator::ConfidenceWeighted:
            default:
            {
                const double wLsq = diff < 2.0 ? 0.70 : 0.30;
                out.velocityMidiPerSec = wLsq * ls + (1.0 - wLsq) * med;
                break;
            }
        }

        out.velocityCentsPerSec = out.velocityMidiPerSec * 100.0;
        out.direction  = out.velocityMidiPerSec >  0.3 ? 1.0
                       : out.velocityMidiPerSec < -0.3 ? -1.0
                       :                                  0.0;
        out.confidence = std::max (0.0, 1.0 - diff / 10.0);

        // Acceleration: slope over the inner half vs. the full window
        if (n >= 4)
        {
            double ms[kCapacity], ts[kCapacity];
            const int ns = readWindow (windowSec * 0.5, ms, ts, kCapacity);
            if (ns >= 2)
            {
                const double innerSlope = leastSquaresSlope (ms, ts, ns);
                const double halfWin    = windowSec * 0.5;
                if (halfWin > 1e-9)
                    out.acceleration = (innerSlope - ls) / halfWin;
            }
        }

        // Vibrato: sign reversal in adjacent-pair slopes
        bool hasPos = false, hasNeg = false;
        for (int i = 1; i < n; ++i)
        {
            const double dt = t[i] - t[i - 1];
            if (dt < 1e-12) continue;
            const double v = (m[i] - m[i - 1]) / dt;
            if (v >  1.5) hasPos = true;
            if (v < -1.5) hasNeg = true;
        }
        out.isVibrato = hasPos && hasNeg;

        return out;
    }
};
