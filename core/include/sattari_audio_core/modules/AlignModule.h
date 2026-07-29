#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <vector>

// =============================================================================
//  AlignModule — Sattari Align's DSP, extracted so the standalone plugin and a
//  Mix rack slot run the SAME aligner.
//
//  Time-aligns the Main signal to a Guide signal (the external sidechain) using
//  onset-envelope cross-correlation. Works on anything with transients: vocal
//  doubles, guitar layers, drum stems, stereo pairs.
//
//    1. Compute a fast onset envelope on both signals (rectified differentiated
//       energy, decimated to 200 Hz).
//    2. Every ~200 ms, normalised cross-correlation between recent envelopes
//       gives the integer envelope-lag that best aligns the two.
//    3. Convert lag to samples and apply as a smoothed delay on the Main output.
//
//  Main is ALWAYS delayed by `baseLatencySamples` + smoothedLagSamples, so the
//  lag can swing negative as well as positive without the delay going below
//  zero. latencySamples() reports the fixed base so host delay-compensation has
//  a stable number to work with — it deliberately does NOT include the moving
//  lag, which is the alignment itself and must not be compensated away.
//
//  Algorithm 2 (DTW Warp) is v2 alpha: a constrained-band DTW produces a
//  piecewise-linear warp path the audio loop interpolates, so the delay can
//  follow local drift instead of applying one bulk shift.
//
//  Like SideChainMasterModule this is a two-signal module and depends on
//  setSidechain(). With no Guide routed it degrades to a pure latency —
//  correlation against silence never passes the sensitivity gate — which is the
//  honest behaviour: there is nothing to align to.
// =============================================================================

namespace sattari::dsp
{

class AlignModule final : public SattariModule
{
public:
    static juce::StringArray algorithmChoices() { return { "Bulk Shift (v1)", "DTW Warp (v2 alpha)" }; }

    const char* moduleId()    const noexcept override { return "align"; }
    const char* displayName() const noexcept override { return "Align"; }

    // UI snapshot (lock-free POD; safe to read off the audio thread).
    struct LiveSnapshot
    {
        float detectedOffsetMs = 0.0f;   // smoothed lag applied to Main (signed)
        float confidence = 0.0f;         // peak correlation in [0, 1]
        bool  guideActive = false;       // true when the Guide bus delivered audio
        bool  locked = false;            // true when the lag is held (manual or auto-lock)
    };

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using P = juce::AudioParameterFloat;
        using R = juce::NormalisableRange<float>;

        out.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (prefix + "autoAlign", 1), "Auto Align", true));
        out.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (prefix + "lock",      1), "Lock Alignment", false));
        out.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (prefix + "bypass",    1), "Bypass", false));
        // v1 = bulk-shift (single lag for the whole stream), v2 = DTW warping
        // (time-varying delay that follows local drift). v2 is alpha - the warp
        // path is recomputed every search cadence and smoothed into the delay so
        // sudden warp changes can pump on transients.
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "algorithm", 1), "Algorithm", algorithmChoices(), 0));
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "manualOffsetMs", 1), "Manual Offset",
                                            R (-150.0f, 150.0f, 0.1f), 0.0f));
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "searchRangeMs", 1), "Search Range",
                                            R (10.0f, 300.0f, 1.0f), 100.0f));
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "sensitivity", 1), "Sensitivity",
                                            R (0.0f, 1.0f, 0.01f), 0.55f));
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "smoothing", 1), "Smoothing",
                                            R (0.0f, 1.0f, 0.01f), 0.7f));
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int maxBlockSize, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        preparedBlockSize = juce::jmax (1, maxBlockSize);

        // Onset-envelope ring (200 Hz internal rate) + decimation counter.
        mainEnv.assign ((size_t) envHistorySize, 0.0f);
        guideEnv.assign ((size_t) envHistorySize, 0.0f);
        envSamplesPerHost = juce::jmax (1, (int) std::round (sr / (double) envSampleRate));

        // Light smoothing on the rectified envelope; sharp enough to keep transients.
        mainSmoother.prepare (sr, 4.0f, 30.0f);
        guideSmoother.prepare (sr, 4.0f, 30.0f);

        // Reserve enough delay headroom for the maximum search range (300 ms).
        maxDelaySamples = (int) std::ceil (0.300 * sr) + preparedBlockSize + 16;
        delayLineL.assign ((size_t) maxDelaySamples, 0.0f);
        delayLineR.assign ((size_t) maxDelaySamples, 0.0f);

        // Preallocate the DTW scratch to its worst case (radius <= envWindowSize/2, so
        // W <= envWindowSize + 1) so computeDtwWarp() never allocates on the audio thread.
        dtwCost.assign ((size_t) envWindowSize * (size_t) (envWindowSize + 1), 0.0);
        dtwRev.clear();
        dtwRev.reserve ((size_t) envWindowSize + 4);

        // Half the worst-case search range, so the detected lag can swing both ways
        // without ever requiring a negative delay.
        baseLatencySamples = (int) std::ceil (0.150 * sr);

        reset();
    }

    void reset() override
    {
        std::fill (mainEnv.begin(), mainEnv.end(), 0.0f);
        std::fill (guideEnv.begin(), guideEnv.end(), 0.0f);
        std::fill (delayLineL.begin(), delayLineL.end(), 0.0f);
        std::fill (delayLineR.begin(), delayLineR.end(), 0.0f);
        delayWritePos = 0;
        envWritePos = 0;
        envAccumulator = 0;
        mainEnvAccum = guideEnvAccum = 0.0f;
        mainEnvCount = guideEnvCount = 0;
        mainPrevEnergy = guidePrevEnergy = 0.0f;
        smoothedLagSamples = 0.0f;
        samplesUntilSearch = 0;
        dtwClockSec = 0.0;
        {
            const juce::ScopedLock l (warpPathLock);
            warpPath.clear();
        }
        publishedOffsetMs.store (0.0f, std::memory_order_relaxed);
        publishedConfidence.store (0.0f, std::memory_order_relaxed);
        guideHasAudio.store (false, std::memory_order_relaxed);
        locked.store (false, std::memory_order_relaxed);
    }

    // The Guide track. Align is the one module for which a missing sidechain is not
    // a graceful fallback but an absent input — see the header note.
    void setSidechain (const juce::AudioBuffer<float>* sc) noexcept override { sidechain = sc; }

    // The FIXED base only. The moving lag is the alignment; compensating it away
    // would undo the plugin.
    int latencySamples() const noexcept override { return baseLatencySamples; }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (state == nullptr) return;
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples <= 0 || numChannels <= 0) return;

        juce::AudioBuffer<float> emptyGuide;
        const juce::AudioBuffer<float>& guideBuffer =
            (sidechain != nullptr && sidechain->getNumChannels() > 0
             && sidechain->getNumSamples() >= numSamples) ? *sidechain : emptyGuide;

        // True bypass: skip all alignment work but keep the reported base latency so the
        // host's PDC chain stays sample-aligned. Slew the alignment lag back to zero so
        // toggling bypass doesn't click, then fall through to the shared delay-output loop.
        const bool bypassed = p ("bypass") > 0.5f;
        if (bypassed)
        {
            smoothedLagSamples += 0.02f * (0.0f - smoothedLagSamples);
        }
        else
        {
            const bool  autoAlign   = p ("autoAlign") > 0.5f;
            const bool  lockOn      = p ("lock") > 0.5f;
            const float manualMs    = p ("manualOffsetMs");
            const float sensitivity = p ("sensitivity");
            const float smoothing   = p ("smoothing");
            locked.store (lockOn, std::memory_order_relaxed);

            updateOnsetEnvelope (buffer, guideBuffer, numSamples);

            const bool useDtw = juce::roundToInt (p ("algorithm")) == 1;

            samplesUntilSearch -= numSamples;
            if (samplesUntilSearch <= 0 && autoAlign && ! lockOn && guideHasAudio.load (std::memory_order_relaxed))
            {
                if (useDtw)
                {
                    // v2 alpha: full DTW. Path drives the per-sample interpolation below.
                    computeDtwWarp();
                    // Confidence reuses the cross-corr peak as a proxy (cheaper than
                    // computing path cost normalisation here on the audio thread).
                    float conf = 0.0f;
                    (void) searchBestLagFrames (conf);
                    publishedConfidence.store (conf, std::memory_order_relaxed);
                }
                else
                {
                    float conf = 0.0f;
                    const int lagFrames = searchBestLagFrames (conf);
                    const double lagSec = (double) lagFrames / (double) envSampleRate;
                    const float  lagSamples = (float) (lagSec * sr);
                    publishedConfidence.store (conf, std::memory_order_relaxed);
                    if (conf >= sensitivity * 0.6f)
                    {
                        const float alpha = 1.0f - juce::jlimit (0.0f, 0.99f, smoothing);
                        const float blend = juce::jmax (alpha, 0.05f);
                        smoothedLagSamples += blend * (lagSamples - smoothedLagSamples);
                    }
                }
                samplesUntilSearch = (int) (0.2 * sr);
            }
            else if (! autoAlign || lockOn)
            {
                const float target = (float) (manualMs * 0.001 * sr);
                smoothedLagSamples += 0.02f * (target - smoothedLagSamples);
            }

            // For DTW, the published "offset" is the lag at the current clock; for
            // bulk-shift, the smoothed lag.
            if (useDtw)
            {
                float lagNow = smoothedLagSamples;
                {
                    const juce::ScopedTryLock l (warpPathLock);
                    if (l.isLocked() && warpPath.size() >= 2)
                    {
                        // Find segment around dtwClockSec.
                        const double t = dtwClockSec;
                        int hi = 1;
                        while (hi < (int) warpPath.size() && warpPath[(size_t) hi].timeSec < t) ++hi;
                        hi = juce::jmin (hi, (int) warpPath.size() - 1);
                        const int lo = juce::jmax (0, hi - 1);
                        const double t0 = warpPath[(size_t) lo].timeSec;
                        const double t1 = warpPath[(size_t) hi].timeSec;
                        const double f  = (t1 > t0) ? juce::jlimit (0.0, 1.0, (t - t0) / (t1 - t0)) : 0.0;
                        lagNow = (float) ((1.0 - f) * warpPath[(size_t) lo].lagSamples
                                           + f      * warpPath[(size_t) hi].lagSamples);
                    }
                }
                // Smooth into the applied lag so the warp path's piecewise jumps don't
                // click. The user's smoothing knob still controls how slewy this is.
                const float alpha = 1.0f - juce::jlimit (0.0f, 0.995f, smoothing);
                smoothedLagSamples += alpha * (lagNow - smoothedLagSamples);
                dtwClockSec += (double) numSamples / sr;
            }

            publishedOffsetMs.store ((float) (smoothedLagSamples / sr * 1000.0), std::memory_order_relaxed);
        }

        // Apply the smoothed delay to the Main output. Total delay = baseLatency +
        // smoothedLagSamples (clamped to the line size).
        const float delayF = juce::jlimit (0.0f,
                                           (float) (maxDelaySamples - 2),
                                           (float) baseLatencySamples + smoothedLagSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = buffer.getSample (0, i);
            const float inR = numChannels > 1 ? buffer.getSample (1, i) : inL;
            delayLineL[(size_t) delayWritePos] = inL;
            delayLineR[(size_t) delayWritePos] = inR;

            // Read with linear interpolation at the fractional delay.
            float readPos = (float) delayWritePos - delayF;
            while (readPos < 0.0f) readPos += (float) maxDelaySamples;
            const int   i0 = ((int) readPos) % maxDelaySamples;
            const int   i1 = (i0 + 1) % maxDelaySamples;
            const float frac = readPos - std::floor (readPos);

            const float outL = delayLineL[(size_t) i0] * (1.0f - frac) + delayLineL[(size_t) i1] * frac;
            const float outR = delayLineR[(size_t) i0] * (1.0f - frac) + delayLineR[(size_t) i1] * frac;

            buffer.setSample (0, i, outL);
            if (numChannels > 1)
                buffer.setSample (1, i, outR);

            if (++delayWritePos >= maxDelaySamples) delayWritePos = 0;
        }
    }

    LiveSnapshot getLiveSnapshot() const noexcept
    {
        LiveSnapshot s;
        s.detectedOffsetMs = publishedOffsetMs.load (std::memory_order_relaxed);
        s.confidence       = publishedConfidence.load (std::memory_order_relaxed);
        s.guideActive      = guideHasAudio.load (std::memory_order_relaxed);
        s.locked           = locked.load (std::memory_order_relaxed);
        return s;
    }

private:
    static constexpr int envSampleRate  = 200;
    static constexpr int envHistorySize = envSampleRate * 6;   // 6 seconds
    static constexpr int envWindowSize  = envSampleRate * 3;   // 3-second cross-corr window

    // Build half-wave-rectified, smoothed energy difference (rough onset envelope)
    // and accumulate into the 200 Hz decimated ring buffers.
    void updateOnsetEnvelope (const juce::AudioBuffer<float>& mainBuf,
                              const juce::AudioBuffer<float>& guideBuf,
                              int numSamples)
    {
        const int mainCh  = mainBuf.getNumChannels();
        const int guideCh = guideBuf.getNumChannels();
        const bool haveGuide = guideCh > 0;

        float guideEnergyThisBlock = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            float mEnergy = 0.0f;
            for (int ch = 0; ch < mainCh; ++ch)
            {
                const float s = mainBuf.getSample (ch, i);
                mEnergy += s * s;
            }
            mEnergy = mEnergy / (float) juce::jmax (1, mainCh);
            const float mFlux = juce::jmax (0.0f, mEnergy - mainPrevEnergy);
            mainPrevEnergy = mEnergy;
            const float mSmoothed = mainSmoother.process (mFlux);

            float gSmoothed = 0.0f;
            if (haveGuide)
            {
                float gEnergy = 0.0f;
                for (int ch = 0; ch < guideCh; ++ch)
                {
                    const float s = guideBuf.getSample (ch, i);
                    gEnergy += s * s;
                }
                gEnergy = gEnergy / (float) juce::jmax (1, guideCh);
                const float gFlux = juce::jmax (0.0f, gEnergy - guidePrevEnergy);
                guidePrevEnergy = gEnergy;
                gSmoothed = guideSmoother.process (gFlux);
                guideEnergyThisBlock += gEnergy;
            }

            mainEnvAccum  += mSmoothed; ++mainEnvCount;
            guideEnvAccum += gSmoothed; ++guideEnvCount;

            if (++envAccumulator >= envSamplesPerHost)
            {
                envAccumulator = 0;
                mainEnv[(size_t) envWritePos]  = mainEnvCount  > 0 ? mainEnvAccum  / (float) mainEnvCount  : 0.0f;
                guideEnv[(size_t) envWritePos] = guideEnvCount > 0 ? guideEnvAccum / (float) guideEnvCount : 0.0f;
                envWritePos = (envWritePos + 1) % envHistorySize;
                mainEnvAccum = guideEnvAccum = 0.0f;
                mainEnvCount = guideEnvCount = 0;
            }
        }

        guideHasAudio.store (guideEnergyThisBlock > 1.0e-7f, std::memory_order_relaxed);
    }

    // Search lag (in envelope frames) within ±searchRangeFrames that maximises the
    // normalised cross-correlation between the two envelope rings. Positive means
    // Main is ahead of Guide.
    int searchBestLagFrames (float& outConfidence) const
    {
        const int searchFrames = juce::jlimit (1, envWindowSize / 2,
            (int) std::round ((double) p ("searchRangeMs") * 0.001 * (double) envSampleRate));

        auto envAt = [] (const std::vector<float>& ring, int idx) -> float
        {
            idx = ((idx % envHistorySize) + envHistorySize) % envHistorySize;
            return ring[(size_t) idx];
        };

        // Reference: the most recent `envWindowSize` of Guide; we shift Main against it.
        const int base = (envWritePos + envHistorySize - envWindowSize) % envHistorySize;

        double gSum = 0.0, gSqSum = 0.0;
        for (int i = 0; i < envWindowSize; ++i)
        {
            const float g = envAt (guideEnv, base + i);
            gSum += g; gSqSum += (double) g * g;
        }
        const double gMean = gSum / (double) envWindowSize;
        const double gVar  = juce::jmax (1.0e-9, gSqSum / (double) envWindowSize - gMean * gMean);

        int    bestLag = 0;
        double bestScore = -2.0;
        for (int lag = -searchFrames; lag <= searchFrames; ++lag)
        {
            double mSum = 0.0, mSqSum = 0.0, mgSum = 0.0;
            for (int i = 0; i < envWindowSize; ++i)
            {
                const float g = envAt (guideEnv, base + i);
                const float m = envAt (mainEnv,  base + i + lag);
                mSum   += m;
                mSqSum += (double) m * m;
                mgSum  += (double) m * g;
            }
            const double mMean = mSum / (double) envWindowSize;
            const double mVar  = juce::jmax (1.0e-9, mSqSum / (double) envWindowSize - mMean * mMean);
            const double cov   = mgSum / (double) envWindowSize - mMean * gMean;
            const double corr  = cov / std::sqrt (mVar * gVar);
            if (corr > bestScore)
            {
                bestScore = corr;
                bestLag = lag;
            }
        }

        outConfidence = (float) juce::jlimit (0.0, 1.0, bestScore);
        return bestLag;
    }

    // Constrained-band DTW between recent Guide and Main envelopes. Builds a
    // piecewise-linear warp path of (time, lagSamples) anchors that the audio loop
    // interpolates each block. Real Sakoe-Chiba band (radius = search frames).
    // Returns 0 on success.
    int computeDtwWarp()
    {
        const int radius = juce::jlimit (1, envWindowSize / 2,
            (int) std::round ((double) p ("searchRangeMs") * 0.001 * (double) envSampleRate));
        const int N = envWindowSize;

        auto envAt = [] (const std::vector<float>& ring, int idx) -> float
        {
            idx = ((idx % envHistorySize) + envHistorySize) % envHistorySize;
            return ring[(size_t) idx];
        };
        const int base = (envWritePos + envHistorySize - envWindowSize) % envHistorySize;

        // 1-D arrays held as flat (i,j) -> cost[(i*(2*radius+1)) + (j-i+radius)].
        const int W = 2 * radius + 1;
        // Reuse the preallocated scratch (sized for the worst-case W in prepare());
        // only the first N*W entries are used this call. No audio-thread allocation.
        auto& cost = dtwCost;
        std::fill (cost.begin(), cost.begin() + (size_t) N * (size_t) W, std::numeric_limits<double>::infinity());
        auto idx = [W, radius] (int i, int j) -> int { return i * W + (j - i + radius); };

        cost[(size_t) idx (0, 0)] = 0.0;
        for (int i = 1; i < N; ++i)
        {
            const float gi = envAt (guideEnv, base + i);
            const int jLo = juce::jmax (0, i - radius);
            const int jHi = juce::jmin (N - 1, i + radius);
            for (int j = jLo; j <= jHi; ++j)
            {
                const float mj = envAt (mainEnv, base + j);
                const double local = std::abs ((double) gi - (double) mj);
                double best = std::numeric_limits<double>::infinity();
                // Three predecessors: (i-1,j-1) diagonal, (i-1,j) advance guide,
                // (i,j-1) advance main. Sakoe-Chiba band bounds j-i to [-radius, +radius].
                if (j - 1 >= juce::jmax (0, (i - 1) - radius))
                    best = juce::jmin (best, cost[(size_t) idx (i - 1, j - 1)]);
                if (std::abs (j - (i - 1)) <= radius)
                    best = juce::jmin (best, cost[(size_t) idx (i - 1, j)]);
                if (j - 1 >= juce::jmax (0, i - radius))
                    best = juce::jmin (best, cost[(size_t) idx (i, j - 1)]);
                cost[(size_t) idx (i, j)] = best + local;
            }
        }

        // Backtrack from (N-1, N-1) - if it's unreachable, find the best terminal in
        // the last row within band.
        int i = N - 1, j = N - 1;
        {
            double bestEnd = std::numeric_limits<double>::infinity();
            for (int jj = juce::jmax (0, i - radius); jj <= juce::jmin (N - 1, i + radius); ++jj)
                if (cost[(size_t) idx (i, jj)] < bestEnd) { bestEnd = cost[(size_t) idx (i, jj)]; j = jj; }
            if (! std::isfinite (bestEnd)) return 1;
        }

        // Walk back; sample every ~50 ms of guide-time so the path is compact.
        const int stride = juce::jmax (1, (int) (envSampleRate * 0.05));
        auto& rev = dtwRev;   // preallocated; reused each call (no audio-thread alloc)
        rev.clear();
        while (i > 0 && j > 0)
        {
            if ((N - 1 - i) % stride == 0)
                rev.push_back ({ (double) i / (double) envSampleRate,
                                 (float) ((double) (j - i) * sr / (double) envSampleRate) });
            const double diag = cost[(size_t) idx (i - 1, j - 1)];
            const double upI  = (std::abs (j - (i - 1)) <= radius) ? cost[(size_t) idx (i - 1, j)] : std::numeric_limits<double>::infinity();
            const double left = (j - 1 >= juce::jmax (0, i - radius)) ? cost[(size_t) idx (i, j - 1)] : std::numeric_limits<double>::infinity();
            if (diag <= upI && diag <= left) { --i; --j; }
            else if (upI <= left)            { --i; }
            else                             { --j; }
        }
        rev.push_back ({ 0.0, (float) ((double) (j - i) * sr / (double) envSampleRate) });

        std::reverse (rev.begin(), rev.end());

        {
            const juce::ScopedLock l (warpPathLock);
            warpPath = rev;
            dtwClockSec = 0.0;
        }
        return 0;
    }

    float p (const char* id) const noexcept
    {
        return cachedParameterValue (id);
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;
    int    preparedBlockSize = 512;

    // --- Onset envelopes (200 Hz internal rate) ------------------------------
    std::vector<float> mainEnv, guideEnv;   // rolling rings
    int    envWritePos = 0;
    int    envSamplesPerHost = 1;
    int    envAccumulator = 0;
    float  mainPrevEnergy = 0.0f, guidePrevEnergy = 0.0f;
    float  mainEnvAccum = 0.0f, guideEnvAccum = 0.0f;
    int    mainEnvCount = 0, guideEnvCount = 0;
    EnvelopeFollower mainSmoother, guideSmoother;

    // --- Delay line on the Main path -----------------------------------------
    int baseLatencySamples = 0;        // reported to the host; the moving lag is not
    int maxDelaySamples = 0;
    std::vector<float> delayLineL, delayLineR;
    int delayWritePos = 0;
    float smoothedLagSamples = 0.0f;   // signed; positive = main delayed more

    // --- DTW warp state (v2 alpha) -------------------------------------------
    struct WarpAnchor { double timeSec; float lagSamples; };
    std::vector<WarpAnchor> warpPath;
    juce::CriticalSection warpPathLock;
    double dtwClockSec = 0.0;
    std::vector<double>     dtwCost;
    std::vector<WarpAnchor> dtwRev;

    // --- Detection cadence + output state ------------------------------------
    int samplesUntilSearch = 0;
    const juce::AudioBuffer<float>* sidechain = nullptr;
    std::atomic<float> publishedOffsetMs { 0.0f };
    std::atomic<float> publishedConfidence { 0.0f };
    std::atomic<bool>  guideHasAudio { false };
    std::atomic<bool>  locked { false };
};

} // namespace sattari::dsp
