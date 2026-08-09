#pragma once

// LiveLoudnessMeter — running BS.1770 / EBU R128 loudness on a WORKER THREAD.
//
// The audio callback only push()es the master buffer through a lock-free FIFO; all
// K-weighting, block gating, and the integrated fold happen on our own thread, so
// no analysis ever runs in the audio callback (per the band spec's requirement).
//
// Exposes momentary (400ms) and gated integrated LUFS plus a sample peak, as
// atomics the UI reads at its own rate. reset() restarts integration (e.g. at the
// start of a session) so the integrated number reflects the current mix, not history.

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <cmath>

namespace stemdeck
{

class LiveLoudnessMeter : private juce::Thread
{
public:
    LiveLoudnessMeter() : juce::Thread ("StemDeck LUFS") {}
    ~LiveLoudnessMeter() override { stop(); }

    void prepare (double sr, int numChannels)
    {
        stop();
        sampleRate = sr > 0.0 ? sr : 44100.0;
        channels   = juce::jlimit (1, 2, numChannels);
        blockLen   = juce::jmax (1, (int) std::round (0.1 * sampleRate));   // 100 ms hop
        ring.setSize (2, kFifoCap);
        ring.clear();
        fifo.setTotalSize (kFifoCap);
        fifo.reset();
        resetIntegration();
        startThread (juce::Thread::Priority::low);
    }

    void stop()
    {
        signalThreadShouldExit();
        notify();
        stopThread (1500);
    }

    // Audio thread — RT-safe: no locks, no allocation, just a ring-buffer copy.
    void push (const juce::AudioBuffer<float>& master) noexcept
    {
        const int n = master.getNumSamples();
        if (n <= 0 || master.getNumChannels() <= 0) return;

        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        auto blit = [&] (int dst, int src, int cnt)
        {
            for (int c = 0; c < 2; ++c)
            {
                const int sc = juce::jmin (c, master.getNumChannels() - 1);
                ring.copyFrom (c, dst, master, sc, src, cnt);
            }
        };
        if (n1 > 0) blit (s1, 0, n1);
        if (n2 > 0) blit (s2, n1, n2);
        fifo.finishedWrite (n1 + n2);
        notify();
    }

    void resetIntegration()
    {
        const juce::ScopedLock sl (dataLock);
        histogramCount.fill (0);
        histogramEnergy.fill (0.0);
        recent.fill (0.0);
        recentCount = recentWrite = 0;
        accL = accR = 0.0; filled = 0; curPeak = 0.0; sinceRecompute = 0;
        integrated.store (-300.0);
        momentary.store  (-300.0);
        peakDb.store     (-300.0);
    }

    double getIntegratedLufs() const noexcept { return integrated.load (std::memory_order_relaxed); }
    double getMomentaryLufs()  const noexcept { return momentary.load  (std::memory_order_relaxed); }
    double getPeakDb()         const noexcept { return peakDb.load     (std::memory_order_relaxed); }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void highpass (double fc, double Q, double sr)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * fc / sr, c = std::cos (w), al = std::sin (w) / (2 * Q);
            const double a0 = 1 + al; b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
        }
        void highShelf (double fc, double gainDb, double Q, double sr)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2.0 * juce::MathConstants<double>::pi * fc / sr, c = std::cos (w), sn = std::sin (w);
            const double al = sn / (2.0 * Q), sA = std::sqrt (A);
            const double a0 =        (A + 1) - (A - 1) * c + 2 * sA * al;
            b0 = A * ((A + 1) + (A - 1) * c + 2 * sA * al) / a0;
            b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0;
            b2 = A * ((A + 1) + (A - 1) * c - 2 * sA * al) / a0;
            a1 = 2 * ((A - 1) - (A + 1) * c) / a0;
            a2 = ((A + 1) - (A - 1) * c - 2 * sA * al) / a0;
        }
    };

    static double loudness (double z) noexcept { return z > 0.0 ? -0.691 + 10.0 * std::log10 (z) : -300.0; }

    void run() override
    {
        // K-weighting filters (BS.1770), fresh state per channel.
        Biquad sL, hL, sR, hR;
        sL.highShelf (1681.9744509555319, 3.99984385397, 0.7071752369554196, sampleRate);
        sR = sL;
        hL.highpass (38.13547087613982, 0.5, sampleRate);
        hR = hL;

        while (! threadShouldExit())
        {
            const int ready = fifo.getNumReady();
            if (ready <= 0) { wait (30); continue; }

            int s1, n1, s2, n2;
            fifo.prepareToRead (ready, s1, n1, s2, n2);
            auto chew = [&] (int src, int cnt)
            {
                const float* L = ring.getReadPointer (0, src);
                const float* R = ring.getReadPointer (1, src);
                for (int i = 0; i < cnt; ++i)
                {
                    const double kl = hL.process (sL.process ((double) L[i]));
                    const double kr = hR.process (sR.process ((double) R[i]));
                    accL += kl * kl; accR += kr * kr;
                    const double tp = juce::jmax (std::abs ((double) L[i]), std::abs ((double) R[i]));
                    if (tp > curPeak) curPeak = tp;
                    if (++filled >= blockLen)
                    {
                        finishBlock();
                        accL = accR = 0.0; filled = 0;
                    }
                }
            };
            if (n1 > 0) chew (s1, n1);
            if (n2 > 0) chew (s2, n2);
            fifo.finishedRead (n1 + n2);

            peakDb.store (curPeak > 0.0 ? 20.0 * std::log10 (curPeak) : -300.0, std::memory_order_relaxed);
            curPeak *= 0.98;   // slow peak decay
        }
    }

    void finishBlock()
    {
        // z of this 100ms block (channel weights = 1 for L/R), mean-square.
        const double z100 = (accL + accR) / (double) blockLen;

        // Momentary = mean over the last 400ms (4 × 100ms hops).
        recent[(size_t) recentWrite] = z100;
        recentWrite = (recentWrite + 1) % (int) recent.size();
        recentCount = juce::jmin ((int) recent.size(), recentCount + 1);
        double m = 0.0;
        for (int i = 0; i < recentCount; ++i) m += recent[(size_t) i];
        m /= (double) recentCount;
        momentary.store (loudness (m), std::memory_order_relaxed);

        // Integrated gating operates on 400ms windows at a 100ms hop. A fixed
        // 0.1-LU histogram keeps sessions bounded: the old vector grew forever and
        // copied/scanned the entire show every 400 ms (quadratic work over time).
        {
            const juce::ScopedLock sl (dataLock);
            const double lu = loudness (m);
            if (lu >= kMinLufs)
            {
                const int bin = juce::jlimit (0, kHistogramBins - 1,
                    (int) std::floor ((juce::jmin (lu, kMaxLufs) - kMinLufs) / kBinWidth));
                ++histogramCount[(size_t) bin];
                histogramEnergy[(size_t) bin] += m;
            }
        }
        if (++sinceRecompute >= 10) { sinceRecompute = 0; recomputeIntegrated(); }
    }

    void recomputeIntegrated()
    {
        const juce::ScopedLock sl (dataLock);
        double absSum = 0.0;
        std::uint64_t absCount = 0;
        for (int bin = 0; bin < kHistogramBins; ++bin)
        {
            absSum += histogramEnergy[(size_t) bin];
            absCount += histogramCount[(size_t) bin];
        }
        if (absCount == 0) { integrated.store (-300.0, std::memory_order_relaxed); return; }
        const double rel = loudness (absSum / (double) absCount) - 10.0;
        double sum = 0.0;
        std::uint64_t cnt = 0;
        for (int bin = 0; bin < kHistogramBins; ++bin)
        {
            const double binLufs = kMinLufs + ((double) bin + 0.5) * kBinWidth;
            if (binLufs >= rel)
            {
                sum += histogramEnergy[(size_t) bin];
                cnt += histogramCount[(size_t) bin];
            }
        }
        integrated.store (cnt ? loudness (sum / (double) cnt) : -300.0, std::memory_order_relaxed);
    }

    static constexpr int kFifoCap = 1 << 16;   // 65536 samples (~1.5s at 44.1k) of headroom
    double sampleRate = 44100.0;
    int    channels = 2, blockLen = 4410;

    juce::AbstractFifo fifo { kFifoCap };
    juce::AudioBuffer<float> ring;

    static constexpr double kMinLufs = -70.0, kMaxLufs = 20.0, kBinWidth = 0.1;
    static constexpr int kHistogramBins = (int) ((kMaxLufs - kMinLufs) / kBinWidth) + 1;
    juce::CriticalSection dataLock;   // guards integration histogram (worker vs. reset)
    std::array<std::uint64_t, kHistogramBins> histogramCount {};
    std::array<double, kHistogramBins> histogramEnergy {};
    std::array<double, 4> recent {};  // last four 100ms z's (for momentary)
    int recentCount = 0, recentWrite = 0;
    double accL = 0.0, accR = 0.0, curPeak = 0.0;
    int    filled = 0, sinceRecompute = 0;

    std::atomic<double> integrated { -300.0 };
    std::atomic<double> momentary  { -300.0 };
    std::atomic<double> peakDb     { -300.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveLoudnessMeter)
};

} // namespace stemdeck
