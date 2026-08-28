#pragma once

//  SattariPeriodShifter — period-synchronous pitch shifting at tracking latency.
//
//  WHY THIS EXISTS
//
//  Our fastest pitch path was PSOLA at 680 samples (15.4 ms at 44.1 kHz). Antares
//  Auto-Tune's published low-latency figure is 112 samples (2.5 ms), and its Classic
//  mode is roughly 37. That 6x gap is architectural, not a tuning problem: PSOLA needs
//  two pitch periods in hand to place a grain, so its latency is
//  ceil(sr/minHz * 2) + 1 and no amount of optimisation moves it.
//
//  Auto-Tune does something structurally different, and it is public. US5973252A
//  (Hildebrand, filed 1998) is EXPIRED, and Hildebrand himself confirmed on the record
//  — correcting Sean Costello, who had attributed Auto-Tune to Lent's granular method —
//  that the maths in the patent is exactly what the product does. The method is:
//
//    * detect the period by recursive autocorrelation at TWO LAGS ONLY (zero and L),
//      on an 8x decimated signal, so the estimate is available continuously rather
//      than per analysis window;
//    * correct by resampling at a variable rate, and when the read pointer drifts a
//      full period away from the write pointer, REPEAT one period (to raise) or SKIP
//      one period (to lower).
//
//  The consequence that makes it fast, and the thing that is easy to miss: DETECTION
//  LATENCY DOES NOT ADD TO OUTPUT LATENCY. The resampler reads the undelayed input
//  directly; the detector only supplies a period estimate, and a slightly stale estimate
//  is harmless because pitch moves slowly relative to a block. So the output path costs
//  only the interpolator's half-width plus a small guard band — tens of samples, not
//  thousands. PSOLA cannot do this because its output is synthesised FROM the analysis.
//
//  WHAT THIS IS NOT. Repeating and deleting whole periods is audibly cruder than PSOLA
//  on large shifts — it is the 1997 sound, which is precisely why it is the right engine
//  for a tracking/monitoring path and the wrong one for a print path. Keep PSOLA and
//  Rubber Band for print. See SattariPsolaShifter.h.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sattari::dsp
{

// Continuously-updated fundamental-period estimator.
//
// Two lags only, per the patent: energy at lag 0 and correlation at lag L. Runs on an
// 8x decimated signal so the correlation sums are cheap enough to maintain every hop,
// which is what makes the estimate continuous rather than windowed.
class PeriodTracker
{
public:
    void prepare (double sampleRate, double minHz, double maxHz)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        decimatedRate = sr / (double) kDecimation;

        // Search bounds in DECIMATED samples.
        minLag = std::max (2, (int) std::floor (decimatedRate / std::max (1.0, maxHz)));
        maxLag = std::max (minLag + 2, (int) std::ceil (decimatedRate / std::max (1.0, minHz)));

        // Window covers two of the longest periods so the longest lag still has a full
        // period of overlap to correlate against.
        windowLen = maxLag * 2 + 4;

        int size = 1;
        while (size < windowLen * 2) size <<= 1;
        mask = size - 1;
        ring.assign ((size_t) size, 0.0f);

        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        writePos = 0;
        filterState.fill (0.0f);
        decimationPhase = 0;
        hopCounter = 0;
        currentLag = 0.0;
        currentConfidence = 0.0f;
        filled = 0;
    }

    // Feed input at the FULL rate. Cheap: one FIR accumulate per sample, and a
    // correlation update only once per decimated hop.
    void push (const float* in, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            // Half-band-ish decimation FIR. Short on purpose — its group delay costs
            // detection responsiveness, never output latency.
            for (int t = (int) kFirTaps - 1; t > 0; --t)
                filterState[(size_t) t] = filterState[(size_t) (t - 1)];
            filterState[0] = in[i];

            if (++decimationPhase < kDecimation)
                continue;
            decimationPhase = 0;

            float acc = 0.0f;
            for (size_t t = 0; t < kFirTaps; ++t)
                acc += filterState[t] * kFir[t];

            ring[(size_t) (writePos & mask)] = acc;
            ++writePos;
            if (filled < windowLen) ++filled;

            if (++hopCounter >= kHopDecimated)
            {
                hopCounter = 0;
                updateEstimate();
            }
        }
    }

    // Period in FULL-rate samples, or 0 when no pitch is being tracked.
    double periodSamples() const noexcept
    {
        return currentLag > 0.0 ? currentLag * (double) kDecimation : 0.0;
    }
    float confidence() const noexcept { return currentConfidence; }

private:
    void updateEstimate()
    {
        if (filled < windowLen) return;

        // Normalised autocorrelation, evaluated over a search range. The patent's
        // "two lags" refers to what is computed PER CANDIDATE — energy (lag 0) and
        // cross-correlation (lag L) — not to evaluating a single candidate. We narrow
        // the candidate range around the current estimate once locked, which is what
        // makes this affordable at every hop.
        int lo = minLag, hi = maxLag;
        if (currentLag > 0.0)
        {
            const int centre = (int) std::lround (currentLag);
            const int span = std::max (3, centre / 8);
            lo = std::max (minLag, centre - span);
            hi = std::min (maxLag, centre + span);
        }

        double bestScore = -1.0;
        int bestLag = 0;
        std::vector<double> scores ((size_t) (hi - lo + 1), -1.0);
        for (int lag = lo; lag <= hi; ++lag)
        {
            double cross = 0.0, energyA = 0.0, energyB = 0.0;
            const int span = maxLag;   // fixed comparison span keeps lags commensurable
            for (int k = 0; k < span; ++k)
            {
                const double a = ring[(size_t) ((writePos - 1 - k) & mask)];
                const double b = ring[(size_t) ((writePos - 1 - k - lag) & mask)];
                cross   += a * b;
                energyA += a * a;
                energyB += b * b;
            }
            const double denom = std::sqrt (energyA * energyB);
            if (denom < 1.0e-9) continue;
            const double score = cross / denom;
            scores[(size_t) (lag - lo)] = score;
            if (score > bestScore) { bestScore = score; bestLag = lag; }
        }

        // OCTAVE CORRECTION. Autocorrelation scores lag 2T as highly as lag T — a
        // perfectly periodic signal correlates with itself at every multiple of its
        // period — so plain argmax lands an octave (or two) low. Measured before this
        // guard: a 147 Hz tone tracked as 73.5 Hz, exactly one octave down, at
        // confidence 1.000. Prefer the SHORTEST lag that is nearly as good, which is the
        // standard sub-harmonic fix and the same shape YIN's absolute threshold takes.
        if (bestLag > 0)
        {
            for (int lag = lo; lag < bestLag; ++lag)
            {
                if (scores[(size_t) (lag - lo)] >= bestScore * kSubHarmonicPreference)
                {
                    bestLag = lag;
                    bestScore = scores[(size_t) (lag - lo)];
                    break;
                }
            }
        }

        // Below threshold means unvoiced or silent: hold the last period rather than
        // chasing noise, and report low confidence so callers can fall through to dry.
        if (bestLag <= 0 || bestScore < kVoicingThreshold)
        {
            currentConfidence = 0.0f;
            if (currentLag > 0.0) return;      // hold
            currentLag = 0.0;
            return;
        }

        // Parabolic refinement around the winner for sub-sample resolution.
        double refined = (double) bestLag;
        if (bestLag > minLag && bestLag < maxLag)
        {
            const double ym1 = correlationAt (bestLag - 1);
            const double y0  = bestScore;
            const double yp1 = correlationAt (bestLag + 1);
            const double denom = 2.0 * (2.0 * y0 - ym1 - yp1);
            if (std::abs (denom) > 1.0e-12)
                refined += (yp1 - ym1) / denom;
        }
        refined = std::clamp (refined, (double) minLag, (double) maxLag);

        // Light smoothing: the resampler wants a stable period, and a single bad hop
        // should not throw the grid. Fast enough to follow a real glide.
        currentLag = currentLag > 0.0 ? currentLag + 0.35 * (refined - currentLag) : refined;
        currentConfidence = (float) std::clamp (bestScore, 0.0, 1.0);
    }

    double correlationAt (int lag) const
    {
        double cross = 0.0, energyA = 0.0, energyB = 0.0;
        const int span = maxLag;
        for (int k = 0; k < span; ++k)
        {
            const double a = ring[(size_t) ((writePos - 1 - k) & mask)];
            const double b = ring[(size_t) ((writePos - 1 - k - lag) & mask)];
            cross   += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const double denom = std::sqrt (energyA * energyB);
        return denom < 1.0e-9 ? 0.0 : cross / denom;
    }

    static constexpr int    kDecimation      = 8;
    static constexpr int    kHopDecimated    = 16;    // ~128 full-rate samples between updates
    static constexpr double kVoicingThreshold = 0.30;
    // How much correlation a shorter (higher) candidate may give up and still win. 0.85
    // rejects true octave errors without chasing noise up into the harmonics.
    static constexpr double kSubHarmonicPreference = 0.85;
    static constexpr size_t kFirTaps         = 17;
    // Windowed-sinc low-pass at ~ Nyquist/8, normalised to unity DC gain.
    static constexpr float  kFir[kFirTaps] = {
        0.0032f, 0.0083f, 0.0186f, 0.0345f, 0.0555f, 0.0797f, 0.1037f, 0.1220f,
        0.1290f,
        0.1220f, 0.1037f, 0.0797f, 0.0555f, 0.0345f, 0.0186f, 0.0083f, 0.0032f };

    double sr = 44100.0, decimatedRate = 5512.5;
    int minLag = 2, maxLag = 128, windowLen = 260;
    std::vector<float> ring;
    int64_t writePos = 0;
    int mask = 0, filled = 0;
    std::array<float, kFirTaps> filterState {};
    int decimationPhase = 0, hopCounter = 0;
    double currentLag = 0.0;
    float currentConfidence = 0.0f;
};

// The shifter proper: a variable-rate resampler that repeats or skips whole periods to
// stay in step with the input.
class PeriodShifter
{
public:
    void prepare (double sampleRate, double minHz = 60.0, double maxHz = 1000.0)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        maxPeriod = std::max (4.0, sr / std::max (1.0, minHz));
        tracker.prepare (sr, minHz, maxHz);

        // The ring must hold enough past for a full period repeat plus the interpolator
        // kernel, with slack for a block. Only the GUARD below is latency; the rest is
        // history we already have.
        int want = (int) std::ceil (maxPeriod * 4.0) + 8192;
        int size = 1;
        while (size < want) size <<= 1;
        mask = size - 1;
        ring.assign ((size_t) size, 0.0f);

        // THE LATENCY, and the whole point of this class.
        //
        // Output latency is the interpolator's left half-width plus a guard so the read
        // pointer can run ahead of nominal without passing the write pointer between
        // period corrections. It is NOT a function of minHz: the period estimate arrives
        // from the tracker, which is delayed but off the output path entirely.
        latency = kInterpHalf + kGuard;
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        tracker.reset();
        writePos = 0;
        readPos = -(double) latency;
        fadeFromPos = 0.0;
        fadeRemaining = 0;
        primed = false;
    }

    int getLatencySamples() const noexcept { return latency; }
    double detectedPeriodSamples() const noexcept { return tracker.periodSamples(); }
    float detectionConfidence() const noexcept { return tracker.confidence(); }

    // n in, n out. `pitchScale` is multiplicative (2.0 = up an octave). At exactly 1.0
    // this is sample-accurate passthrough of the delayed input — no resampling, no
    // period edits, so unity costs nothing but the delay.
    void process (const float* in, float* out, int n, double pitchScale)
    {
        tracker.push (in, n);

        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) (writePos & mask)] = in[i];
            ++writePos;

            const double period = tracker.periodSamples();
            const double scale = (pitchScale > 0.0) ? pitchScale : 1.0;
            const double nominal = (double) writePos - (double) latency;

            // The read pointer must never reach the write pointer: past it is unwritten
            // ring, which reads as stale audio from the previous lap. This bound — not a
            // fraction of the period — is what decides when a repeat is due. Sizing the
            // trigger off the period instead was a real bug: at 220 Hz, 0.5 * period is
            // 100 samples of allowed drift against only 61 samples of headroom, so the
            // pointer ran 39 samples past the write head every cycle and the "pitch
            // shift" was partly stale ring content.
            const double maxSafeRead = (double) writePos - (double) (kInterpHalf + 1);

            if (period > 2.0)
            {
                if (readPos > maxSafeRead - kJumpMargin)
                    jumpBy (-period);             // reading too fast (pitch up): repeat
                else if (readPos < nominal - 1.5 * period)
                    jumpBy (period);              // reading too slow (pitch down): skip
            }
            else
            {
                // Unvoiced or silent: no period to splice on, so hold the nominal delay
                // and pass through. Splicing on a wrong period is what makes naive
                // implementations buzz on consonants.
                readPos = nominal;
                fadeRemaining = 0;
            }

            // Crossfade the splice. A hard jump is only inaudible if the period estimate
            // is exact; ours is smoothed and sub-sample refined, so the waveform does not
            // line up perfectly and a bare jump clicks. The fade also stops the glitch
            // from adding spurious zero crossings, which was inflating the measured shift
            // ratio in the contract test by up to 130 cents.
            if (fadeRemaining > 0)
            {
                const double t = 1.0 - ((double) fadeRemaining / (double) kFadeLen);
                const float a = readAt (fadeFromPos);
                const float b = readAt (readPos);
                out[i] = (float) (a * (1.0 - t) + b * t);
                fadeFromPos += scale;
                --fadeRemaining;
            }
            else
            {
                out[i] = readAt (readPos);
            }
            readPos += scale;
        }
        primed = true;
    }

private:
    // Start a period splice: the old trajectory keeps running for the length of the
    // crossfade so the two can be mixed rather than cut between.
    void jumpBy (double delta) noexcept
    {
        fadeFromPos = readPos;
        readPos += delta;
        fadeRemaining = kFadeLen;
    }

    // 4-point Catmull-Rom. Cheap, and its half-width is what sets our latency floor.
    float readAt (double position) const
    {
        const double floorPos = std::floor (position);
        const double frac = position - floorPos;
        const int64_t base = (int64_t) floorPos;
        const float ym1 = ring[(size_t) ((base - 1) & mask)];
        const float y0  = ring[(size_t) ( base      & mask)];
        const float y1  = ring[(size_t) ((base + 1) & mask)];
        const float y2  = ring[(size_t) ((base + 2) & mask)];
        const double t = frac, t2 = t * t, t3 = t2 * t;
        return (float) (0.5 * ((2.0 * y0)
                             + (-ym1 + y1) * t
                             + (2.0 * ym1 - 5.0 * y0 + 4.0 * y1 - y2) * t2
                             + (-ym1 + 3.0 * y0 - 3.0 * y1 + y2) * t3));
    }

    static constexpr int kInterpHalf = 2;    // Catmull-Rom needs one sample either side
    static constexpr int kGuard      = 62;   // headroom for inter-correction drift
    static constexpr int kJumpMargin = 4;    // trigger the repeat before the bound, not at it
    static constexpr int kFadeLen    = 48;   // ~1.1 ms splice crossfade at 44.1 kHz

    PeriodTracker tracker;
    double sr = 44100.0, maxPeriod = 735.0;
    std::vector<float> ring;
    int64_t writePos = 0;
    double readPos = 0.0;
    int mask = 0, latency = 64;
    double fadeFromPos = 0.0;
    int fadeRemaining = 0;
    bool primed = false;
};

} // namespace sattari::dsp
