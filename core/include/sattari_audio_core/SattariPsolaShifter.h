#pragma once

// =============================================================================
//  Sattari Audio Suite — streaming TD-PSOLA pitch shifter (low-latency monitor)
// -----------------------------------------------------------------------------
//  Time-Domain Pitch-Synchronous Overlap-Add. This is the autotalent / classic
//  vocal-tuner engine: it repositions pitch-synchronous grains (one fundamental
//  period each, Hann-windowed, 2 periods wide) at a new spacing, which changes
//  the fundamental WITHOUT resampling the grain — so the spectral envelope
//  (formants) is carried through unchanged. Two advantages over a phase vocoder:
//    * Near-zero latency (~2 pitch periods, a handful of ms) — usable for a
//      singer monitoring through the plugin while tracking.
//    * Naturally formant-preserving, no separate envelope estimation.
//  The trade-off is lower fidelity than Rubber Band on sustained/complex
//  material, which is why the suite keeps Rubber Band for the printed sound and
//  uses PSOLA only for the live-monitor path.
//
//  Pure std, no JUCE, header-only, unit-tested in
//  core/tests/psola_shifter_contract.cpp. Feed it mono blocks plus the detected
//  pitch period and a pitch scale; it returns the same number of samples per
//  block, delayed by getLatencySamples().
// =============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sattari::dsp
{

class PsolaShifter
{
public:
    void prepare (double sampleRate, double minHz = 60.0, double maxHz = 1000.0)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        minPeriod = std::max (2.0, sr / std::max (1.0, maxHz));
        maxPeriod = std::max (minPeriod + 1.0, sr / std::max (1.0, minHz));

        // Ring must hold several max-periods of history/lookahead plus a block.
        int want = (int) std::ceil (maxPeriod * 6.0) + 4096;
        int size = 1;
        while (size < want) size <<= 1;
        mask = size - 1;
        inRing.assign ((size_t) size, 0.0f);
        outRing.assign ((size_t) size, 0.0f);
        normRing.assign ((size_t) size, 0.0f);

        // Fixed internal latency = 2 max-periods, so process() can always return a
        // full, fully-overlapped block. Pre-load the output FIFO with that many
        // zeros (silence during priming) so the delay is constant and reported.
        latency = (int) std::ceil (maxPeriod * 2.0) + 1;
        reset();
    }

    void reset()
    {
        std::fill (inRing.begin(),   inRing.end(),   0.0f);
        std::fill (outRing.begin(),  outRing.end(),  0.0f);
        std::fill (normRing.begin(), normRing.end(), 0.0f);
        writePos   = 0;
        outReadPos = -(int64_t) latency;   // start `latency` samples behind the input
        nextSynth  = 0.0;
        primed     = false;
    }

    int getLatencySamples() const noexcept { return latency; }

    // n input samples -> n output samples (the pitch-shifted signal delayed by
    // getLatencySamples()). `periodSamples` is the detected fundamental period of
    // the CURRENT input; `pitchScale` is the multiplicative shift (2.0 = up an
    // octave). Pass pitchScale==1 for a transparent (identity) pass at unity.
    void process (const float* in, float* out, int n, double pitchScale, double periodSamples)
    {
        if (mask <= 0 || n <= 0)
        {
            for (int i = 0; i < n; ++i) out[i] = in ? in[i] : 0.0f;
            return;
        }

        const double P     = std::clamp (periodSamples, minPeriod, maxPeriod);
        const double alpha = std::clamp (pitchScale, 0.5, 2.0);
        const double synthHop = std::max (1.0, P / alpha);

        // 1) Write the input block into the ring.
        for (int i = 0; i < n; ++i)
            inRing[(size_t) ((writePos + i) & mask)] = in[i];
        writePos += n;

        // 2) Place every synthesis grain that is now fully supported by input.
        //    A grain at synth position s reads an analysis window centred at the
        //    nearest analysis mark a = round(s/P)*P over [a-P, a+P], so we need
        //    input available up to a+P (~ s + 1.5P). Stop before that edge.
        const double supported = (double) writePos - (P + 2.0);
        while (nextSynth <= supported)
        {
            placeGrain (nextSynth, P);
            nextSynth += synthHop;
        }

        // 3) Emit n finalised output samples (outRing / window-sum), clearing as
        //    we consume so the ring can be reused. outReadPos trails writePos by
        //    `latency`, so everything up to here is fully overlap-added.
        for (int i = 0; i < n; ++i)
        {
            const int64_t p = outReadPos + i;
            float y = 0.0f;
            if (p >= 0)
            {
                const size_t idx = (size_t) (p & mask);
                const float w = normRing[idx];
                y = (w > 1.0e-6f) ? (outRing[idx] / w) : 0.0f;
                outRing[idx]  = 0.0f;    // consume
                normRing[idx] = 0.0f;
            }
            out[i] = y;
        }
        outReadPos += n;
    }

private:
    // Stamp one Hann-windowed, pitch-synchronous grain: read a 2P window centred
    // at the nearest analysis mark, overlap-add it into the output at the synth
    // position. Grain content is raw input -> formants ride through unchanged.
    void placeGrain (double s, double P)
    {
        const int    half = (int) std::floor (P);
        if (half < 1) return;
        // Nearest analysis mark: pick the mark INDEX n = round(s/P), then place it at
        // round(n*P). The old form round(s/P)*round(P) multiplied the index by the
        // INTEGER-rounded period, so for any non-integer P (i.e. essentially every
        // voiced pitch) the mark drifted n*(round(P)-P) samples off the input and the
        // grain fell outside the ring within seconds — the monitor faded to silence.
        const int64_t a  = (int64_t) std::llround ((double) std::llround (s / P) * P);   // nearest analysis mark
        const int64_t sc = (int64_t) std::llround (s);                                  // synth centre
        const int64_t oldest = writePos - (int64_t) inRing.size() + 2;

        const double twoHalf = 2.0 * (double) half;
        for (int k = -half; k <= half; ++k)
        {
            const int64_t ai = a + k;
            if (ai < oldest || ai >= writePos) continue;      // outside ring history
            // Hann over the 2P window (k in [-half, +half] -> [0, 2half]).
            const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) (k + half) / twoHalf);
            const float  x = inRing[(size_t) (ai & mask)];
            const int64_t oi = sc + k;
            const size_t  oidx = (size_t) (oi & mask);
            outRing[oidx]  += (float) w * x;
            normRing[oidx] += (float) w;
        }
    }

    double sr = 44100.0;
    double minPeriod = 48.0, maxPeriod = 735.0;
    int    mask = 0;
    int    latency = 0;
    int64_t writePos = 0;
    int64_t outReadPos = 0;
    double  nextSynth = 0.0;
    bool    primed = false;
    std::vector<float> inRing, outRing, normRing;
};

} // namespace sattari::dsp
