#pragma once

// =============================================================================
//  Sattari Live Pitch Engine — causal low-latency foundation (alpha)
// -----------------------------------------------------------------------------
//  This is deliberately separate from the shipping Auto Pitch Studio HQ path.
//  It provides the two foundations a transparent live tuner needs:
//
//    1. A causal, decimated YIN tracker with voiced-state hysteresis. It uses no
//       future audio and performs no allocation after prepare().
//    2. A waveform-locked TD-PSOLA shifter. Analysis marks are refined against
//       the actual waveform instead of assuming that pitch epochs lie on a grid
//       beginning at sample zero (the phase-drift failure of the legacy path).
//
//  Unity and unvoiced audio use an exact fixed delayed-dry reference while the
//  epoch/OLA path stays warm. This makes correction Off and 1.0x mathematically
//  transparent and protects consonants before a later transient classifier is
//  added. The class is pure std, header-only and not wired to plugin parameters.
// =============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace sattari::dsp
{

class LivePitchTracker
{
public:
    struct Result
    {
        double frequencyHz = 0.0;
        double periodSamples = 0.0; // at the original sample rate
        float confidence = 0.0f;
        float rms = 0.0f;
        bool voiced = false;
    };

    void prepare (double sampleRate, double minimumHz = 100.0,
                  double maximumHz = 1100.0, int hopSamples = 256)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        minHz = std::clamp (minimumHz, 50.0, 400.0);
        maxHz = std::clamp (maximumHz, minHz + 1.0, 2000.0);
        decimation = sr >= 70000.0 ? 4 : (sr >= 32000.0 ? 2 : 1);
        analysisSr = sr / (double) decimation;
        minLag = std::max (2, (int) std::floor (analysisSr / maxHz));
        maxLag = std::max (minLag + 2, (int) std::ceil (analysisSr / minHz));

        // Slightly more than two periods at the bottom of the selected range.
        // At 48 kHz / 100 Hz this is 512 decimated samples = 21.3 ms of history.
        length = 1;
        while (length < 2 * maxLag + 4) length <<= 1;
        length = std::clamp (length, 256, 4096);
        maxLag = std::min (maxLag, length / 2 - 2);

        ring.assign ((size_t) length, 0.0f);
        linear.assign ((size_t) length, 0.0f);
        yin.assign ((size_t) (maxLag + 2), 1.0f);
        hopDecimated = std::max (16, hopSamples / decimation);
        reset();
    }

    void reset() noexcept
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        write = 0;
        filled = 0;
        samplesToAnalysis = hopDecimated;
        decimationPhase = 0;
        decimationAccumulator = 0.0;
        candidateCount = 0;
        dropoutCount = 0;
        medianCount = 0;
        medianWrite = 0;
        latest = {};
    }

    Result process (const float* input, int numSamples) noexcept
    {
        if (input == nullptr || numSamples <= 0 || ring.empty())
            return latest;

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = input[i];
            // Causal box filter before decimation. Analysis stays near 24 kHz at
            // 44.1, 48 and 96 kHz, keeping both CPU and detector behavior stable.
            decimationAccumulator += (double) x;
            if (++decimationPhase < decimation)
                continue;
            const float filtered = (float) (decimationAccumulator / (double) decimation);
            decimationAccumulator = 0.0;
            decimationPhase = 0;

            ring[(size_t) write] = filtered;
            write = (write + 1) % length;
            filled = std::min (filled + 1, length);
            if (--samplesToAnalysis <= 0)
            {
                samplesToAnalysis = hopDecimated;
                // reset() zero-fills the ring, so with onset acceleration a
                // partly filled ring is simply a frame with leading silence
                // (e.g. transport started mid-note).
                if (filled == length || onsetAcceleration)
                    analyseFrame();
            }
        }
        return latest;
    }

    Result getResult() const noexcept { return latest; }
    // Opt-in faster acquisition after silence (see analyseFrame). Off by
    // default, so LivePitchEngine and existing callers are unchanged.
    void setOnsetAcceleration (bool shouldAccelerate) noexcept { onsetAcceleration = shouldAccelerate; }
    double getMinimumHz() const noexcept { return minHz; }
    int analysisWindowSamples() const noexcept { return length * decimation; }
    int analysisHopSamples() const noexcept { return hopDecimated * decimation; }

private:
    void analyseFrame() noexcept
    {
        for (int i = 0; i < length; ++i)
            linear[(size_t) i] = ring[(size_t) ((write + i) % length)];

        double energy = 0.0;
        for (float x : linear) energy += (double) x * (double) x;
        const float rms = (float) std::sqrt (energy / (double) length);
        latest.rms = rms;
        if (rms < 1.0e-5f)
        {
            acceptUnvoiced();
            return;
        }

        // Onset acceleration (opt-in): a frame whose oldest part is still the
        // silence before a note is first analysed from the note's first sample.
        // Leading zeros contribute nothing periodic, but a full-length frame
        // only becomes confident once the window has mostly filled with the
        // note (21 ms for a 100 Hz floor). The trimmed pass only searches lags
        // the trimmed frame can verify (>= 2.5 candidate periods), has no
        // global-minimum fallback and rejects a dip on the range edge. When it
        // finds nothing confident the legacy full-frame pass runs, so the
        // legacy acquisition time is the worst case. It also runs once voiced,
        // until the frame has filled, so the voiced state cannot chatter on
        // mixed silence/note frames.
        const float required = latest.voiced ? 0.42f : 0.60f;
        int best = -1;
        int lagLimit = maxLag;
        if (onsetAcceleration)
        {
            float peak = 0.0f;
            for (float x : linear) peak = std::max (peak, std::abs (x));
            const float gate = 0.08f * peak;
            int start = 0;
            while (start < length && std::abs (linear[(size_t) start]) < gate) ++start;
            start = std::max (0, start - 2);
            const int trimmedLength = length - start;
            const int trimmedLimit = std::min (maxLag, (int) ((double) trimmedLength / 2.5));
            if (start > length / 8 && trimmedLimit >= minLag + 3)
            {
                best = yinSearch (linear.data() + start, trimmedLength, trimmedLimit, false);
                lagLimit = trimmedLimit;
                if (best >= minLag && 1.0f - yin[(size_t) best] < required)
                    best = -1;
            }
        }
        if (best < minLag)
        {
            best = yinSearch (linear.data(), length, maxLag, true);
            lagLimit = maxLag;
        }

        const float confidence = best >= minLag
            ? std::clamp (1.0f - yin[(size_t) best], 0.0f, 1.0f) : 0.0f;
        // Entry remains stricter than hold so breaths cannot chatter the voiced
        // switch. The values are deliberately below clean-sine confidence: real
        // close-mic vocals with saturation or room energy commonly sit around
        // 0.45-0.70 even when their fundamental is musically unambiguous.
        if (best < minLag || confidence < required)
        {
            latest.confidence = confidence;
            acceptUnvoiced();
            return;
        }

        double lag = (double) best;
        if (best > minLag && best < lagLimit)
        {
            const double a = yin[(size_t) (best - 1)];
            const double b = yin[(size_t) best];
            const double c = yin[(size_t) (best + 1)];
            const double denominator = a - 2.0 * b + c;
            if (std::abs (denominator) > 1.0e-12)
                lag += 0.5 * (a - c) / denominator;
        }

        double hz = analysisSr / lag;
        if (latest.voiced && latest.frequencyHz > 0.0)
        {
            while (hz > latest.frequencyHz * 1.62) hz *= 0.5;
            while (hz < latest.frequencyHz * 0.617) hz *= 2.0;
        }
        hz = std::clamp (hz, minHz, maxHz);

        recentHz[(size_t) medianWrite] = hz;
        medianWrite = (medianWrite + 1) % (int) recentHz.size();
        medianCount = std::min (medianCount + 1, (int) recentHz.size());
        std::array<double, 3> sorted = recentHz;
        std::sort (sorted.begin(), sorted.begin() + medianCount);
        hz = sorted[(size_t) (medianCount / 2)];

        dropoutCount = 0;
        candidateCount = std::min (candidateCount + 1, 8);
        latest.frequencyHz = hz;
        latest.periodSamples = sr / hz;
        latest.confidence = confidence;
        latest.voiced = latest.voiced || candidateCount >= 2;
    }

    // Cumulative-mean-normalised difference over lags 1..lagLimit with a fixed
    // comparison length (every lag gets the same statistical weight). Returns
    // the first dip under the threshold, else (when allowed) the global
    // minimum, else -1. A dip on the edge of a restricted range is rejected.
    int yinSearch (const float* frame, int frameLength, int lagLimit, bool allowFallback) noexcept
    {
        const int compare = frameLength - lagLimit - 1;
        yin[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= lagLimit; ++tau)
        {
            // A 512-sample frame at vocal level has ample float headroom. Keeping
            // this inner accumulation in float lets Clang vectorise the hot YIN
            // loop; double arithmetic here pushed the analysis callback just over
            // a 48 kHz / 64-sample deadline without improving the selected lag.
            float difference = 0.0f;
            const float* const delayed = frame + tau;
            for (int i = 0; i < compare; ++i)
            {
                const float d = frame[i] - delayed[i];
                difference += d * d;
            }
            running += (double) difference;
            yin[(size_t) tau] = (float) (running > 1.0e-18
                ? (double) difference * (double) tau / running : 1.0);
        }

        constexpr float threshold = 0.16f;
        int best = -1;
        for (int tau = minLag; tau < lagLimit; ++tau)
        {
            if (yin[(size_t) tau] < threshold)
            {
                while (tau + 1 <= lagLimit && yin[(size_t) (tau + 1)] < yin[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }
        if (! allowFallback)
            return best >= lagLimit - 1 ? -1 : best;
        if (best < 0)
        {
            float score = std::numeric_limits<float>::max();
            for (int tau = minLag; tau <= lagLimit; ++tau)
                if (yin[(size_t) tau] < score) { score = yin[(size_t) tau]; best = tau; }
        }
        return best;
    }

    void acceptUnvoiced() noexcept
    {
        candidateCount = 0;
        if (++dropoutCount >= 3)
        {
            latest.voiced = false;
            latest.frequencyHz = 0.0;
            latest.periodSamples = 0.0;
            medianCount = 0;
            medianWrite = 0;
        }
    }

    double sr = 48000.0, analysisSr = 24000.0;
    double minHz = 100.0, maxHz = 1100.0;
    int decimation = 2, decimationPhase = 0;
    int minLag = 10, maxLag = 240, length = 512;
    int write = 0, filled = 0;
    int hopDecimated = 128, samplesToAnalysis = 128;
    double decimationAccumulator = 0.0;
    int candidateCount = 0, dropoutCount = 0;
    std::array<double, 3> recentHz {};
    int medianCount = 0, medianWrite = 0;
    std::vector<float> ring, linear, yin;
    Result latest;
    bool onsetAcceleration = false;
};

class LiveEpochShifter
{
public:
    void prepare (double sampleRate, double minimumHz = 100.0,
                  double maximumHz = 1100.0, int maximumBlockSize = 2048)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        minPeriod = std::max (4.0, sr / std::max (maximumHz, minimumHz + 1.0));
        maxPeriod = std::max (minPeriod + 2.0, sr / std::max (minimumHz, 1.0));

        // One full period for the grain plus a quarter-period epoch-search margin.
        // 1.5 periods leaves a small scheduling cushion without hiding a long FIFO.
        latency = (int) std::ceil (1.5 * maxPeriod) + 8;
        int wanted = latency + (int) std::ceil (8.0 * maxPeriod) + maximumBlockSize + 64;
        int size = 1;
        while (size < wanted) size <<= 1;
        mask = size - 1;
        inputRing.assign ((size_t) size, 0.0f);
        outputRing.assign ((size_t) size, 0.0f);
        normalisationRing.assign ((size_t) size, 0.0f);
        epochs.assign (512, Epoch {});
        constexpr int windowTableSize = 4097;
        constexpr double pi = 3.1415926535897932384626433832795;
        hannTable.resize (windowTableSize);
        for (int i = 0; i < windowTableSize; ++i)
            hannTable[(size_t) i] = (float) (0.5 - 0.5 * std::cos (
                2.0 * pi * (double) i / (double) (windowTableSize - 1)));
        wetCoefficient = 1.0 - std::exp (-1.0 / (0.0025 * sr));
        reset();
    }

    void reset() noexcept
    {
        std::fill (inputRing.begin(), inputRing.end(), 0.0f);
        std::fill (outputRing.begin(), outputRing.end(), 0.0f);
        std::fill (normalisationRing.begin(), normalisationRing.end(), 0.0f);
        writePosition = 0;
        readPosition = -(int64_t) latency;
        epochStart = 0;
        epochCount = 0;
        predictedEpoch = 0.0;
        nextSynthesisEpoch = 0.0;
        smoothedPeriod = sr / 180.0;
        wasVoiced = false;
        wetMix = 0.0;
    }

    int getLatencySamples() const noexcept { return latency; }
    double getLatencyMilliseconds() const noexcept { return 1000.0 * latency / sr; }

    // periodSamples and voiced come from a causal detector. At unity, or while
    // unvoiced, output is the exact delayed input. The epoch path is still updated
    // at unity so enabling correction does not begin from a cold phase estimate.
    void process (const float* input, float* output, int numSamples,
                  double pitchScale, double periodSamples, bool voiced) noexcept
    {
        if (input == nullptr || output == nullptr || numSamples <= 0 || mask <= 0)
            return;

        for (int i = 0; i < numSamples; ++i)
            inputRing[(size_t) ((writePosition + i) & mask)] = input[i];
        writePosition += numSamples;

        if (voiced && periodSamples >= minPeriod && periodSamples <= maxPeriod)
        {
            if (! wasVoiced)
                smoothedPeriod = periodSamples;
            else
            {
                const double ratio = std::clamp (periodSamples / smoothedPeriod, 0.80, 1.25);
                smoothedPeriod *= std::pow (ratio, 0.35);
            }
            smoothedPeriod = std::clamp (smoothedPeriod, minPeriod, maxPeriod);
            if (! wasVoiced)
            {
                clearEpochs();
                predictedEpoch = std::max (0.0, (double) writePosition - 1.25 * smoothedPeriod);
                nextSynthesisEpoch = predictedEpoch;
            }
            discoverEpochs();
            renderSupportedEpochs (std::clamp (pitchScale, 0.5, 2.0));
        }
        else
        {
            clearEpochs();
            predictedEpoch = (double) writePosition;
            nextSynthesisEpoch = (double) writePosition;
        }
        wasVoiced = voiced;

        const double correctionCents = 1200.0 * std::log2 (std::max (pitchScale, 1.0e-12));
        const double targetWet = voiced && std::abs (correctionCents) > 0.10 ? 1.0 : 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const int64_t position = readPosition + i;
            float dry = 0.0f, shifted = 0.0f;
            if (position >= 0)
            {
                const size_t index = (size_t) (position & mask);
                dry = inputRing[index];
                const float weight = normalisationRing[index];
                shifted = weight > 1.0e-5f ? outputRing[index] / weight : dry;
                outputRing[index] = 0.0f;
                normalisationRing[index] = 0.0f;
            }
            // A 2.5 ms dezipper protects voiced/unvoiced boundaries and note
            // attacks. Starting from reset at unity remains exactly dry.
            wetMix += wetCoefficient * (targetWet - wetMix);
            output[i] = (float) ((double) dry + wetMix * ((double) shifted - (double) dry));
        }
        readPosition += numSamples;
        discardOldEpochs();
    }

private:
    struct Epoch { int64_t position = 0; double period = 0.0; };

    float inputAt (int64_t position) const noexcept
    {
        const int64_t oldest = writePosition - (int64_t) inputRing.size();
        if (position < oldest || position >= writePosition) return 0.0f;
        return inputRing[(size_t) (position & mask)];
    }

    void clearEpochs() noexcept { epochStart = 0; epochCount = 0; }

    void appendEpoch (int64_t position, double period) noexcept
    {
        if (epochs.empty()) return;
        if (epochCount == (int) epochs.size())
        {
            epochStart = (epochStart + 1) % (int) epochs.size();
            --epochCount;
        }
        epochs[(size_t) ((epochStart + epochCount) % (int) epochs.size())] = { position, period };
        ++epochCount;
    }

    const Epoch& epochAt (int index) const noexcept
    {
        return epochs[(size_t) ((epochStart + index) % (int) epochs.size())];
    }

    void discoverEpochs() noexcept
    {
        const double radius = std::clamp (0.22 * smoothedPeriod, 3.0, 0.35 * maxPeriod);
        while (predictedEpoch + radius + 2.0 < (double) writePosition)
        {
            const int64_t lo = (int64_t) std::floor (predictedEpoch - radius);
            const int64_t hi = (int64_t) std::ceil  (predictedEpoch + radius);
            int64_t best = lo;
            double bestScore = -1.0;
            for (int64_t p = lo; p <= hi; ++p)
            {
                // Glottal closures appear as high-energy differentiated peaks.
                // Absolute polarity supports microphones wired either way.
                const double x = inputAt (p);
                const double derivative = x - 0.97 * (double) inputAt (p - 1);
                const double curvature = x - 0.5 * ((double) inputAt (p - 1) + inputAt (p + 1));
                const double score = std::abs (derivative) + 0.35 * std::abs (curvature)
                                   + 0.05 * std::abs (x);
                if (score > bestScore) { bestScore = score; best = p; }
            }

            if (epochCount > 0)
            {
                const int64_t previous = epochAt (epochCount - 1).position;
                const double spacing = (double) (best - previous);
                if (spacing < 0.55 * smoothedPeriod)
                    best = previous + (int64_t) std::llround (smoothedPeriod);
                else if (spacing > 1.55 * smoothedPeriod)
                    best = previous + (int64_t) std::llround (smoothedPeriod);
            }

            appendEpoch (best, smoothedPeriod);
            if (epochCount == 1) nextSynthesisEpoch = (double) best;
            predictedEpoch = (double) best + smoothedPeriod;
        }
    }

    void renderSupportedEpochs (double pitchScale) noexcept
    {
        if (epochCount <= 0) return;
        const double supported = (double) writePosition - smoothedPeriod - 2.0;
        int guard = 0;
        while (nextSynthesisEpoch <= supported && guard++ < 8192)
        {
            const Epoch* nearest = nullptr;
            double distance = std::numeric_limits<double>::max();
            for (int i = 0; i < epochCount; ++i)
            {
                const auto& candidate = epochAt (i);
                const double d = std::abs ((double) candidate.position - nextSynthesisEpoch);
                if (d < distance) { distance = d; nearest = &candidate; }
            }
            if (nearest != nullptr)
                placeGrain (*nearest, nextSynthesisEpoch);
            nextSynthesisEpoch += std::max (2.0, smoothedPeriod / pitchScale);
        }
    }

    void placeGrain (const Epoch& analysis, double synthesisPosition) noexcept
    {
        const int half = std::max (4, (int) std::floor (analysis.period));
        const int64_t synthesis = (int64_t) std::llround (synthesisPosition);
        for (int k = -half; k <= half; ++k)
        {
            const int64_t source = analysis.position + k;
            const int64_t destination = synthesis + k;
            if (source < writePosition - (int64_t) inputRing.size() || source >= writePosition)
                continue;
            if (destination < readPosition)
                continue;
            const double unitPosition = (double) (k + half) / (2.0 * (double) half);
            const int windowIndex = std::clamp (
                (int) std::llround (unitPosition * (double) (hannTable.size() - 1)),
                0, (int) hannTable.size() - 1);
            const float window = hannTable[(size_t) windowIndex];
            const size_t outIndex = (size_t) (destination & mask);
            outputRing[outIndex] += window * inputAt (source);
            normalisationRing[outIndex] += window;
        }
    }

    void discardOldEpochs() noexcept
    {
        const int64_t oldestNeeded = readPosition - (int64_t) std::ceil (2.0 * maxPeriod);
        while (epochCount > 0 && epochAt (0).position < oldestNeeded)
        {
            epochStart = (epochStart + 1) % (int) epochs.size();
            --epochCount;
        }
    }

    double sr = 48000.0, minPeriod = 43.6, maxPeriod = 480.0;
    double smoothedPeriod = 266.7;
    int latency = 728, mask = 0;
    int64_t writePosition = 0, readPosition = 0;
    double predictedEpoch = 0.0, nextSynthesisEpoch = 0.0;
    bool wasVoiced = false;
    double wetMix = 0.0, wetCoefficient = 0.0;
    std::vector<float> inputRing, outputRing, normalisationRing, hannTable;
    std::vector<Epoch> epochs;
    int epochStart = 0, epochCount = 0;
};

class LiveCorrectionPlanner
{
public:
    struct Settings
    {
        // Pitch classes C..B. This is the direct bridge from Sattari Key; keeping
        // it as a mask also supports custom note buttons without another planner.
        std::array<bool, 12> enabledNotes {
            true, true, true, true, true, true,
            true, true, true, true, true, true
        };
        double amount = 1.0;                 // 0=dry pitch, 1=full correction
        double retuneMilliseconds = 20.0;    // correction trajectory time constant
        double preserveVibrato = 0.75;       // 0=flatten residual, 1=retain it
        double noteHysteresisCents = 18.0;   // advantage required before note switch
        double transitionAmount = 1.0;       // 0=sustain speed, 1=fast note changes
        int forcedTargetMidi = -1;           // >=0: explicit graph/MIDI note target
        // Generated pitch motion applied after correction strength. This is the
        // bridge for Auto Pitch's Add Vibrato control; it remains independent of
        // Amount exactly as it is in the Studio engine.
        double pitchOffsetCents = 0.0;
        //  Concert reference the integer MIDI grid is anchored to. The snap target
        //  comes from nearestEnabledMidi() on this grid, so a hardcoded 440 pulled
        //  an A=432 session 31.8 cents sharp - the plugin's own Reference Detune /
        //  Follow Song Reference must reach the live planner too.
        double referenceHz = 440.0;
    };

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept
    {
        centreMidi = 0.0;
        correctionCents = 0.0;
        targetMidi = -1;
        voiced = false;
    }

    double update (const LivePitchTracker::Result& pitch, int numSamples,
                   const Settings& settings) noexcept
    {
        if (! pitch.voiced || pitch.frequencyHz <= 0.0 || numSamples <= 0)
        {
            voiced = false;
            targetMidi = -1;
            correctionCents = 0.0;
            return 1.0;
        }

        const double midi = 69.0 + 12.0 * std::log2 (
            pitch.frequencyHz / (settings.referenceHz > 0.0 ? settings.referenceHz : 440.0));
        const int forcedTarget = settings.forcedTargetMidi >= 0
            ? std::clamp (settings.forcedTargetMidi, 0, 127) : -1;
        const auto selectTarget = [&] (double sourceMidi) noexcept
        {
            return forcedTarget >= 0
                ? forcedTarget : nearestEnabledMidi (sourceMidi, settings.enabledNotes);
        };
        bool targetChanged = false;
        if (! voiced)
        {
            centreMidi = midi;
            correctionCents = 0.0;
            targetMidi = selectTarget (midi);
            voiced = true;
        }

        // A real note transition must not be mistaken for huge vibrato around the
        // old note. Re-anchor before the slow centre filter can ask the shifter to
        // drag a new syllable back by several semitones. Ordinary vibrato remains
        // below this threshold and therefore keeps its continuous centre.
        if (std::abs (midi - centreMidi) > 0.80)
        {
            centreMidi = midi;
            const int nextTarget = selectTarget (midi);
            targetChanged = nextTarget != targetMidi;
            targetMidi = nextTarget;
        }

        // Separate the slowly moving sung-note centre from vibrato. Correction
        // acts on the centre; preserveVibrato controls how much of the residual
        // is retained, avoiding the metallic contour-chasing failure mode.
        const double centrePole = std::exp (-(double) numSamples / (0.075 * sr));
        centreMidi = centrePole * centreMidi + (1.0 - centrePole) * midi;
        // Musical note selection follows the current pitch with hysteresis. The
        // slow centre is only a vibrato decomposition signal; using it for note
        // selection can hold the previous note through a real transition and ask
        // for a correction larger than one semitone.
        const int candidate = selectTarget (midi);
        if (targetMidi < 0)
        {
            targetMidi = candidate;
            targetChanged = true;
        }
        else if (candidate != targetMidi)
        {
            const double currentDistance = std::abs (midi - (double) targetMidi);
            const double candidateDistance = std::abs (midi - (double) candidate);
            if (forcedTarget >= 0
                || candidateDistance + std::max (0.0, settings.noteHysteresisCents) / 100.0
                    < currentDistance)
            {
                targetMidi = candidate;
                targetChanged = true;
            }
        }

        const double residualSemitones = midi - centreMidi;
        const double preserve = std::clamp (settings.preserveVibrato, 0.0, 1.0);
        const double desiredSemitones = (double) targetMidi - centreMidi
                                      - (1.0 - preserve) * residualSemitones;
        const double desiredCents = 100.0 * std::clamp (settings.amount, 0.0, 1.0)
                                  * desiredSemitones
                                  + std::clamp (settings.pitchOffsetCents, -1200.0, 1200.0);

        // Even the Hard setting gets a 1.5 ms dezipper. It is short enough to
        // feel immediate and prevents a parameter/note boundary from making a
        // discontinuous pitch-scale step.
        const double transition = std::clamp (settings.transitionAmount, 0.0, 1.0);
        const double requestedMs = std::max (1.5, settings.retuneMilliseconds);
        const double activeMs = targetChanged
            ? requestedMs + transition * (1.5 - requestedMs) : requestedMs;
        const double seconds = 0.001 * activeMs;
        const double pole = std::exp (-(double) numSamples / (seconds * sr));
        correctionCents = pole * correctionCents + (1.0 - pole) * desiredCents;
        correctionCents = std::clamp (correctionCents, -1200.0, 1200.0);
        return std::pow (2.0, correctionCents / 1200.0);
    }

    int getTargetMidi() const noexcept { return targetMidi; }
    double getCorrectionCents() const noexcept { return correctionCents; }

private:
    static int nearestEnabledMidi (double midi, const std::array<bool, 12>& enabled) noexcept
    {
        int best = (int) std::llround (midi);
        double bestDistance = std::numeric_limits<double>::max();
        bool any = false;
        for (int note = (int) std::floor (midi) - 12;
             note <= (int) std::ceil (midi) + 12; ++note)
        {
            const int pitchClass = (note % 12 + 12) % 12;
            if (! enabled[(size_t) pitchClass]) continue;
            any = true;
            const double distance = std::abs (midi - (double) note);
            if (distance < bestDistance
                || (std::abs (distance - bestDistance) < 1.0e-12 && note < best))
            {
                best = note;
                bestDistance = distance;
            }
        }
        return any ? best : (int) std::llround (midi);
    }

    double sr = 48000.0;
    double centreMidi = 0.0;
    double correctionCents = 0.0;
    int targetMidi = -1;
    bool voiced = false;
};

// -----------------------------------------------------------------------------
// ExtremePitchShifter
// -----------------------------------------------------------------------------
// A fixed-2 ms causal monitor shifter for the latency-critical tracking path.
// It reuses the newest completed, causally observed pitch cycle and performs
// pitch-synchronous resampling with epoch correlation, short cycle crossfades,
// and an upward-shift anti-alias guard. Unlike an FFT or look-ahead epoch engine,
// it does not wait for a future pitch period before returning audio. The tracker
// still needs accumulated history; until its estimate is trustworthy, output is
// the exact fixed-delay dry path. This is the distinction between processing
// latency and detector acquisition time.
class ExtremePitchShifter
{
public:
    void prepare (double sampleRate, int maximumBlockSize)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        latency = std::max (16, (int) std::lround (0.002 * sr));
        minimumDelay = std::max (8.0, 0.0005 * sr);
        const double maximumWorkingDelay = std::max (
            minimumDelay + 16.0, 2.0 * (double) latency - minimumDelay);
        int wanted = (int) std::ceil (std::max (maximumWorkingDelay, 0.06 * sr))
                   + std::max (1, maximumBlockSize) + 16;
        int size = 1;
        while (size < wanted) size <<= 1;
        mask = size - 1;
        ring.assign ((size_t) size, 0.0f);
        dryRing.assign ((size_t) size, 0.0f);
        wetCoefficient = 1.0 - std::exp (-1.0 / (0.0015 * sr));
        ratioCoefficient = 1.0 - std::exp (-1.0 / (0.0010 * sr));

        // Formant preservation state is prepared unconditionally so that the
        // option can be switched without allocating on the audio thread.
        // Order ~ sr/1500 (32 at 48 kHz): enough poles to whiten the full band
        // including F3/F4, measured against sr/2000 and sr/1000.
        lpcOrder = std::clamp ((int) std::lround (sr / 1500.0), 12, kMaxLpcOrder);
        lpcWindowLength = std::min (size, std::max (64, (int) std::lround (0.020 * sr)));
        lpcHop = std::max (16, (int) std::lround (0.0025 * sr));
        guardAttack = 1.0 - std::exp (-1.0 / (0.0005 * sr));    // 0.5 ms envelope attack
        guardRelease = 1.0 - std::exp (-1.0 / (0.0300 * sr));   // 30 ms envelope release
        guardOnsetSamples = std::max (1, (int) std::lround (0.040 * sr));
        lpcWindow.assign ((size_t) lpcWindowLength, 0.0f);
        lpcFrame.assign ((size_t) lpcWindowLength, 0.0f);
        constexpr double pi = 3.1415926535897932384626433832795;
        for (int i = 0; i < lpcWindowLength; ++i)
            lpcWindow[(size_t) i] = (float) (0.5 - 0.5 * std::cos (
                2.0 * pi * ((double) i + 0.5) / (double) lpcWindowLength));
        // Gaussian lag window (50 Hz) widens the all-pole peaks so the envelope
        // follows formants rather than individual harmonics of high voices.
        for (int k = 0; k <= kMaxLpcOrder; ++k)
        {
            const double w = 2.0 * pi * 50.0 * (double) k / sr;
            lagWindow[(size_t) k] = std::exp (-0.5 * w * w);
        }
        reset();
    }

    void reset() noexcept
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        std::fill (dryRing.begin(), dryRing.end(), 0.0f);
        writePosition = 0;
        phase = 0.25;
        wetMix = 0.0;
        smoothedRatio = 1.0;
        smoothedPeriod = sr / 180.0;
        currentCycleStart = previousCycleStart = 0;
        hasCycle = false;
        antiAlias1 = antiAlias2 = 0.0;
        wantedRatio = 1.0;
        targetWet = 0.0;
        validPeriod = false;
        resetFormantState();
    }

    int getLatencySamples() const noexcept { return latency; }

    // Formant preservation (off by default). The shifter resamples whole pitch
    // cycles, which scales the spectral envelope by the pitch ratio. With this
    // option the input is whitened by a causal all-pole (LPC) model of the
    // singer's vocal tract before it enters the cycle buffer, the cycles are
    // shifted in that excitation domain, and the shifted excitation is
    // re-coloured by the same model - the classic causal LPC formant
    // correction. Analysis uses only past samples (20 ms window, 2.5 ms hop),
    // so it adds no latency. Unvoiced/unity output stays the exact delayed dry
    // input. Switch it while stopped or before processing: the cycle buffer
    // holds a different signal domain in each mode.
    void setFormantPreservation (bool shouldPreserve) noexcept
    {
        if (shouldPreserve == formantPreservation) return;
        formantPreservation = shouldPreserve;
        resetFormantState();
    }
    bool getFormantPreservation() const noexcept { return formantPreservation; }

    // Legacy one-call form: one control update per call, then render. Callers
    // that feed whole host blocks therefore adapt the cycle period once per
    // block; ExtremeLivePitchEngine instead drives setControl()/render() on a
    // fixed sample-clock grid so its output does not depend on the host block.
    void process (const float* input, float* output, int numSamples,
                  double pitchScale, bool voiced,
                  double sourcePeriodSamples = 0.0) noexcept
    {
        if (input == nullptr || output == nullptr || numSamples <= 0 || mask <= 0)
            return;
        setControl (pitchScale, voiced, sourcePeriodSamples);
        render (input, output, numSamples);
    }

    // Control update. periodSmoothing is the per-update exponent applied to
    // the period ratio (0.45 = the legacy per-call behaviour). snapOnAcquire
    // starts a newly voiced segment at the detected period instead of gliding
    // from the previous note's period (which the output pitch would follow:
    // output f0 = ratio * sr / smoothedPeriod).
    void setControl (double pitchScale, bool voiced, double sourcePeriodSamples,
                     double periodSmoothing = 0.45, bool snapOnAcquire = false) noexcept
    {
        wantedRatio = std::clamp (pitchScale, 0.5, 2.0);
        const double correctionCents = 1200.0 * std::log2 (
            std::max (wantedRatio, 1.0e-12));
        targetWet = voiced && std::abs (correctionCents) > 0.10 ? 1.0 : 0.0;
        const bool wasValid = validPeriod;
        validPeriod = voiced && sourcePeriodSamples >= 24.0
                   && sourcePeriodSamples <= 0.025 * sr;
        if (validPeriod)
        {
            if (snapOnAcquire && ! wasValid)
                smoothedPeriod = sourcePeriodSamples;
            else
            {
                const double periodRatio = std::clamp (
                    sourcePeriodSamples / smoothedPeriod, 0.8, 1.25);
                smoothedPeriod *= std::pow (periodRatio, periodSmoothing);
            }
        }
        else
            hasCycle = false;
    }

    // Period-scaled cycle alignment (see findCycleStart). Off by default so the
    // legacy process() callers (e.g. HarmonyRenderer) render bit-identically;
    // ExtremeLivePitchEngine enables it.
    void setWideCycleAlignment (bool shouldUse) noexcept { wideAlignment = shouldUse; }

    // Renders with the most recent control state. Safe when input == output.
    void render (const float* input, float* output, int numSamples) noexcept
    {
        if (input == nullptr || output == nullptr || numSamples <= 0 || mask <= 0)
            return;

        constexpr double twoPi = 6.283185307179586476925286766559;
        for (int i = 0; i < numSamples; ++i)
        {
            smoothedRatio += ratioCoefficient * (wantedRatio - smoothedRatio);

            // A causal anti-alias guard matters only for upward shifts. Near the
            // correction range (normally < one semitone) its cutoff stays above
            // 20 kHz at 48 kHz; octave harmony lowers it before resampling rather
            // than folding upper vocal harmonics into the audible band.
            const double cutoff = std::min (0.45 * sr, 0.45 * sr / std::max (1.0, smoothedRatio));
            const double lp = 1.0 - std::exp (-twoPi * cutoff / sr);
            double analysisInput = (double) input[i];
            if (formantPreservation)
            {
                if (--lpcCountdown <= 0)
                {
                    lpcCountdown = lpcHop;
                    updateLpc();
                }
                for (int k = 0; k < lpcOrder; ++k)
                    reflection[(size_t) k] += reflectionStep[(size_t) k];
                analysisInput = whiten (analysisInput);
            }
            antiAlias1 += lp * (analysisInput - antiAlias1);
            antiAlias2 += lp * (antiAlias1 - antiAlias2);
            ring[(size_t) (writePosition & mask)] = (float) antiAlias2;
            dryRing[(size_t) (writePosition & mask)] = input[i];

            const float dry = readAbsoluteFrom (
                dryRing, (double) writePosition - (double) latency);
            double shifted = (double) dry;
            if (validPeriod)
            {
                if ((! hasCycle || phase >= 1.0)
                    && writePosition > (std::int64_t) std::ceil (
                        minimumDelay + smoothedPeriod + 4.0))
                {
                    phase -= std::floor (phase);
                    const auto next = findCycleStart (smoothedPeriod);
                    if (! hasCycle)
                    {
                        currentCycleStart = previousCycleStart = next;
                        hasCycle = true;
                    }
                    else
                    {
                        previousCycleStart = currentCycleStart;
                        currentCycleStart = next;
                    }
                }

                const double cycleOffset = phase * smoothedPeriod;
                const double oldCycle = readAbsolute ((double) previousCycleStart + cycleOffset);
                const double newCycle = readAbsolute ((double) currentCycleStart + cycleOffset);
                const double x = std::clamp (phase * 5.0, 0.0, 1.0);
                const double blend = x * x * (3.0 - 2.0 * x);
                shifted = oldCycle + blend * (newCycle - oldCycle);
                phase += smoothedRatio / smoothedPeriod;
            }
            if (formantPreservation)
            {
                // Re-colour the shifted excitation. While unvoiced the filter
                // is kept warm on the delayed excitation and its output is
                // discarded (the dry path is exact).
                const double excitation = validPeriod
                    ? shifted : (double) sampleAt (writePosition - (std::int64_t) latency);
                double coloured = recolour (excitation);
                // Onset guard: whitening and re-colouring run ~2 ms apart, so while
                // the LPC model adapts to a note that starts after a pause the pair
                // can briefly boost a resonance (measured up to +20 dB). For the
                // first ~40 ms of an onset the output may not exceed 1.5x the dry
                // peak envelope. Steady singing is left untouched: a continuous
                // gain guard or ceiling there modulates/clips within the period and
                // measurably shifts F1 (5.8 % -> 7.6 %).
                const double ad = std::abs ((double) dry);
                guardDry += (ad > guardDry ? guardAttack : guardRelease) * (ad - guardDry);
                guardSinceOnset = guardDry < 1.0e-4 ? 0 : guardSinceOnset + 1;
                if (guardSinceOnset < guardOnsetSamples)
                {
                    const double ceiling = 1.5 * std::max (guardDry, ad) + 1.0e-6;
                    coloured = std::clamp (coloured, -ceiling, ceiling);
                }
                if (validPeriod)
                    shifted = coloured;
            }

            wetMix += wetCoefficient * (targetWet - wetMix);
            output[i] = (float) ((double) dry + wetMix * (shifted - (double) dry));
            ++writePosition;
        }
    }

private:
    static constexpr int kMaxLpcOrder = 64;
    // Pre-emphasis narrows the vocal spectrum's dynamic range so the all-pole
    // model also whitens F3/F4; without it a shifted copy of the upper
    // formants survives in the excitation.
    static constexpr double kPreEmphasis = 0.9;

    void resetFormantState() noexcept
    {
        reflection.fill (0.0);
        reflectionStep.fill (0.0);
        analysisState.fill (0.0);
        synthesisState.fill (0.0);
        preEmphasisState = deEmphasisState = 0.0;
        guardDry = 0.0;
        guardSinceOnset = 0;
        lpcCountdown = 0;
        // The cycle buffer must not mix signal domains across a mode switch.
        std::fill (ring.begin(), ring.end(), 0.0f);
        antiAlias1 = antiAlias2 = 0.0;
        hasCycle = false;
    }

    // Autocorrelation LPC (Levinson-Durbin) of the newest raw input, in
    // reflection-coefficient form. The lattice filters stay stable for any
    // |k| < 1, including while the coefficients glide to the new frame.
    void updateLpc() noexcept
    {
        const int n = lpcWindowLength;
        double energy = 0.0;
        float previousSample = sampleAtFrom (dryRing, writePosition - (std::int64_t) n - 1);
        for (int j = 0; j < n; ++j)
        {
            const float raw = sampleAtFrom (dryRing, writePosition - (std::int64_t) n + j);
            const float x = (raw - (float) kPreEmphasis * previousSample) * lpcWindow[(size_t) j];
            previousSample = raw;
            lpcFrame[(size_t) j] = x;
            energy += (double) x * (double) x;
        }
        std::array<double, kMaxLpcOrder + 1> r {};
        std::array<double, kMaxLpcOrder> target {};
        // Below ~-100 dBFS keep gliding towards the previous model.
        if (energy > 1.0e-10 * (double) n)
        {
            const float* const frame = lpcFrame.data();
            for (int lag = 0; lag <= lpcOrder; ++lag)
            {
                float acc = 0.0f;
                for (int j = 0; j + lag < n; ++j)
                    acc += frame[j] * frame[j + lag];
                r[(size_t) lag] = (double) acc * lagWindow[(size_t) lag];
            }
            r[0] *= 1.0 + 1.0e-5;   // -50 dB white-noise floor bounds the whitening gain
            std::array<double, kMaxLpcOrder + 1> a {}, previous {};
            a[0] = 1.0;
            double error = r[0];
            for (int i = 1; i <= lpcOrder && error > 1.0e-18; ++i)
            {
                double acc = r[(size_t) i];
                for (int j = 1; j < i; ++j)
                    acc += a[(size_t) j] * r[(size_t) (i - j)];
                const double k = std::clamp (-acc / error, -0.995, 0.995);
                target[(size_t) (i - 1)] = k;
                previous = a;
                for (int j = 1; j < i; ++j)
                    a[(size_t) j] = previous[(size_t) j] + k * previous[(size_t) (i - j)];
                a[(size_t) i] = k;
                error *= 1.0 - k * k;
            }
        }
        else
        {
            // Silence: relax the model toward flat (identity whitening). Never
            // re-apply the previous glide step: it extrapolated every hop, and over
            // a pause the coefficients ramped past |k| = 1 so the lattices blew up
            // to Inf/NaN on the next note. Holding the old note's model instead
            // mismatched the next note's onset (up to +20 dB transients).
            for (int k = 0; k < lpcOrder; ++k)
                target[(size_t) k] = 0.5 * reflection[(size_t) k];
        }
        for (int k = 0; k < lpcOrder; ++k)
        {
            // Lattice stability needs |k| < 1 at every sample of the glide.
            target[(size_t) k] = std::clamp (target[(size_t) k], -0.995, 0.995);
            reflectionStep[(size_t) k] = (target[(size_t) k] - reflection[(size_t) k]) / (double) lpcHop;
        }

        // Underflow guard for the recursive synthesis lattice during silence.
        double state = 0.0;
        for (int k = 0; k < lpcOrder; ++k) state += std::abs (synthesisState[(size_t) k]);
        if (state < 1.0e-20) synthesisState.fill (0.0);
    }

    // Pre-emphasis then FIR lattice A(z): x -> prediction residual.
    double whiten (double input) noexcept
    {
        const double x = input - kPreEmphasis * preEmphasisState;
        preEmphasisState = input;
        double f = x, b = x;
        for (int i = 0; i < lpcOrder; ++i)
        {
            const double k = reflection[(size_t) i];
            const double previous = analysisState[(size_t) i];
            const double nextF = f + k * previous;
            const double nextB = previous + k * f;
            analysisState[(size_t) i] = b;
            f = nextF;
            b = nextB;
        }
        return f;
    }

    // IIR lattice 1/A(z): exact inverse of whiten() for equal coefficients.
    double recolour (double e) noexcept
    {
        double f = e;
        for (int i = lpcOrder - 1; i >= 0; --i)
        {
            const double k = reflection[(size_t) i];
            f -= k * synthesisState[(size_t) i];
            if (i + 1 < lpcOrder)
                synthesisState[(size_t) (i + 1)] = synthesisState[(size_t) i] + k * f;
        }
        synthesisState[0] = f;
        deEmphasisState = f + kPreEmphasis * deEmphasisState;
        return deEmphasisState;
    }

    float sampleAt (std::int64_t p) const noexcept
    {
        return sampleAtFrom (ring, p);
    }

    float sampleAtFrom (const std::vector<float>& source, std::int64_t p) const noexcept
    {
        if (p < 0 || p >= writePosition) return 0.0f;
        if (p < writePosition - (std::int64_t) source.size()) return 0.0f;
        return source[(size_t) (p & mask)];
    }

    float readAbsolute (double position) const noexcept
    {
        return readAbsoluteFrom (ring, position);
    }

    float readAbsoluteFrom (const std::vector<float>& source, double position) const noexcept
    {
        const auto i1 = (std::int64_t) std::floor (position);
        const double t = position - (double) i1;
        const double y0 = sampleAtFrom (source, i1 - 1), y1 = sampleAtFrom (source, i1);
        const double y2 = sampleAtFrom (source, i1 + 1), y3 = sampleAtFrom (source, i1 + 2);
        // Catmull-Rom interpolation has much lower high-frequency droop than a
        // linear tap while remaining four multiplies and allocation-free.
        const double a = -0.5 * y0 + 1.5 * y1 - 1.5 * y2 + 0.5 * y3;
        const double b = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
        const double c = -0.5 * y0 + 0.5 * y2;
        return (float) (((a * t + b) * t + c) * t + y1);
    }

    // Cycle boundaries are found on the clean signal. In formant mode the cycle
    // ring holds the LPC-whitened excitation, whose flattened spectrum lifts
    // breath noise against the harmonics; correlating on it made the cycle chain
    // jitter (~+8 c median pitch error on a breathy phrase). The dry ring holds
    // the raw input at the same positions.
    float alignSample (std::int64_t p) const noexcept
    {
        return formantPreservation ? sampleAtFrom (dryRing, p) : sampleAt (p);
    }

    std::int64_t findCycleStart (double period) const noexcept
    {
        // The newest complete cycle ends at least minimumDelay behind the write
        // head. Search backwards for a strong differentiated epoch; never search
        // forward into samples that have not arrived.
        const auto latest = writePosition
            - (std::int64_t) std::ceil (minimumDelay + period);
        const auto radius = (std::int64_t) std::ceil ((hasCycle ? 0.14 : 1.0) * period);
        std::int64_t predicted = latest;
        if (hasCycle)
        {
            const double distance = (double) (latest - currentCycleStart);
            const auto cycles = std::max<std::int64_t> (
                0, (std::int64_t) std::floor (distance / period));
            predicted = currentCycleStart
                      + (std::int64_t) std::llround ((double) cycles * period);
        }
        const auto lo = predicted - radius;
        const auto hi = std::min (latest, predicted + radius);
        std::int64_t best = std::min (predicted, latest);
        double bestScore = -1.0;
        for (std::int64_t p = lo; p <= hi; ++p)
        {
            double score = 0.0;
            if (hasCycle && wideAlignment)
            {
                // Mean-removed correlation over up to a full period (+-P/2,
                // capped at +-128 samples). A fixed +-12-sample window sees an
                // almost straight segment of a smooth low note (P = 436 at
                // 110 Hz), so every lag scores ~1 and the cycle chain can
                // random-walk by up to 0.14 P, audible as ~15 ct pitch wobble.
                const int half = std::clamp ((int) std::lround (0.5 * period), 12, 128);
                double so = 0.0, sn = 0.0, soo = 0.0, snn = 0.0, son = 0.0;
                for (int k = -half; k <= half; ++k)
                {
                    const double o = alignSample (currentCycleStart + k);
                    const double n = alignSample (p + k);
                    so += o; sn += n; soo += o * o; snn += n * n; son += o * n;
                }
                const double count = (double) (2 * half + 1);
                const double cov = son - so * sn / count;
                const double vo = std::max (1.0e-12, soo - so * so / count);
                const double vn = std::max (1.0e-12, snn - sn * sn / count);
                score = cov / std::sqrt (vo * vn);
            }
            else if (hasCycle)
            {
                double cross = 0.0, oldPower = 1.0e-12, newPower = 1.0e-12;
                for (int k = -12; k <= 12; ++k)
                {
                    const double oldSample = alignSample (currentCycleStart + k);
                    const double newSample = alignSample (p + k);
                    cross += oldSample * newSample;
                    oldPower += oldSample * oldSample;
                    newPower += newSample * newSample;
                }
                score = cross / std::sqrt (oldPower * newPower);
            }
            else
            {
                const double x = alignSample (p);
                const double derivative = x - 0.97 * (double) alignSample (p - 1);
                const double curvature = x - 0.5 * ((double) alignSample (p - 1)
                                                    + (double) alignSample (p + 1));
                score = std::abs (derivative) + 0.35 * std::abs (curvature);
            }
            if (score > bestScore) { bestScore = score; best = p; }
        }
        return best;
    }

    double sr = 48000.0;
    int latency = 96, mask = 0;
    double minimumDelay = 24.0;
    std::int64_t writePosition = 0;
    double phase = 0.25, wetMix = 0.0, smoothedRatio = 1.0;
    double smoothedPeriod = 266.7;
    double wantedRatio = 1.0, targetWet = 0.0;
    bool validPeriod = false;
    bool wideAlignment = false;
    std::int64_t currentCycleStart = 0, previousCycleStart = 0;
    bool hasCycle = false;
    double wetCoefficient = 0.01, ratioCoefficient = 0.02;
    double antiAlias1 = 0.0, antiAlias2 = 0.0;
    std::vector<float> ring, dryRing;

    bool formantPreservation = false;
    int lpcOrder = 24, lpcWindowLength = 960, lpcHop = 120, lpcCountdown = 0;
    std::vector<float> lpcWindow, lpcFrame;
    std::array<double, kMaxLpcOrder + 1> lagWindow {};
    std::array<double, kMaxLpcOrder> reflection {}, reflectionStep {};
    std::array<double, kMaxLpcOrder> analysisState {}, synthesisState {};
    double preEmphasisState = 0.0, deEmphasisState = 0.0;
    double guardDry = 0.0, guardAttack = 0.5, guardRelease = 0.001;
    int guardSinceOnset = 0, guardOnsetSamples = 720;
};

class ExtremeLivePitchEngine
{
public:
    void prepare (double sampleRate, int maximumBlockSize,
                  double minimumHz = 70.0, double maximumHz = 1100.0)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        tracker.prepare (sr, minimumHz, maximumHz, 256);
        // The full-range detector needs a 42.7 ms frame at 48 kHz to observe
        // two periods at 70 Hz. A second 100 Hz floor halves that acquisition
        // window for ordinary vocal notes and, with onset acceleration, locks
        // a note after silence from its first periods. The full tracker keeps
        // authority in the bass register (see trackedResult()), preserving
        // the advertised range.
        fastTracker.prepare (sr, std::max (100.0, minimumHz), maximumHz, 128);
        fastTracker.setOnsetAcceleration (true);
        shifter.prepare (sr, maximumBlockSize);
        shifter.setWideCycleAlignment (true);
        planner.prepare (sr);

        // Fixed sample-clock control grid (~0.67 ms: 32 samples at 48 kHz).
        // Tracker results, the planner and the shifter's period/ratio are
        // latched only on this grid, carried across callbacks, so the output is
        // identical for every host block size (1 .. any). The period smoother
        // is time-normalised to the legacy 64-sample/48 kHz callback rate
        // (exponent 0.45 per 1.333 ms) that the engine was tuned at.
        controlInterval = std::max (8, (int) std::lround (sr / 1500.0));
        periodSmoothing = 1.0 - std::pow (0.55, (double) controlInterval / (sr / 750.0));
        samplesUntilControl = 0;
    }

    void reset() noexcept
    {
        tracker.reset();
        fastTracker.reset();
        shifter.reset();
        planner.reset();
        fallbackPeriod = sr / 180.0;
        correctionResult = {};
        samplesUntilControl = 0;
    }

    // Follower form: pitchScale is latched on the control grid.
    void process (const float* input, float* output, int numSamples,
                  double pitchScale) noexcept
    {
        if (input == nullptr || output == nullptr || numSamples <= 0)
            return;
        for (int done = 0; done < numSamples;)
        {
            if (samplesUntilControl <= 0)
            {
                const auto result = trackedResult();
                correctionResult = result;
                if (result.voiced && result.periodSamples > 0.0)
                    fallbackPeriod = result.periodSamples;
                shifter.setControl (pitchScale, result.voiced, fallbackPeriod,
                                    periodSmoothing, true);
                samplesUntilControl = controlInterval;
            }
            done += advance (input + done, output + done, numSamples - done);
        }
    }

    void processCorrected (const float* input, float* output, int numSamples,
                           const LiveCorrectionPlanner::Settings& settings,
                           const LivePitchTracker::Result* pitchGuide = nullptr) noexcept
    {
        if (input == nullptr || output == nullptr || numSamples <= 0)
            return;
        const bool guideValid = pitchGuide != nullptr && pitchGuide->voiced
                             && pitchGuide->frequencyHz > 0.0;
        for (int done = 0; done < numSamples;)
        {
            if (samplesUntilControl <= 0)
            {
                // Causal: uses only audio before this grid point.
                const auto tracked = trackedResult();
                correctionResult = guideValid ? *pitchGuide : tracked;
                if (tracked.voiced && tracked.periodSamples > 0.0)
                    fallbackPeriod = tracked.periodSamples;
                else if (guideValid)
                    fallbackPeriod = pitchGuide->periodSamples > 0.0
                        ? pitchGuide->periodSamples : sr / pitchGuide->frequencyHz;
                const double scale = planner.update (correctionResult, controlInterval, settings);
                shifter.setControl (scale, tracked.voiced || guideValid, fallbackPeriod,
                                    periodSmoothing, true);
                samplesUntilControl = controlInterval;
            }
            done += advance (input + done, output + done, numSamples - done);
        }
    }

    // Latency is the same fixed 2 ms in both formant modes: the LPC formant
    // correction is causal and adds no delay.
    int getLatencySamples() const noexcept { return shifter.getLatencySamples(); }
    LivePitchTracker::Result getPitchResult() const noexcept { return correctionResult; }
    int getTargetMidi() const noexcept { return planner.getTargetMidi(); }
    double getCorrectionCents() const noexcept { return planner.getCorrectionCents(); }

    // Optional formant preservation (see ExtremePitchShifter). Off by default,
    // which renders exactly as without the option. Allocation-free; call it
    // from prepare/reset context, not mid-phrase (it clears the cycle buffer).
    void setFormantPreservation (bool shouldPreserve) noexcept { shifter.setFormantPreservation (shouldPreserve); }
    bool getFormantPreservation() const noexcept { return shifter.getFormantPreservation(); }

    // Control-grid introspection for callers that link several channels: a
    // follower fed the lead's ratio in chunks of this length (after the lead
    // processed the same chunk) reproduces the lead's per-grid ratio exactly,
    // independent of the host block size.
    int getControlIntervalSamples() const noexcept { return controlInterval; }
    int getSamplesUntilControlUpdate() const noexcept
    {
        return samplesUntilControl > 0 ? samplesUntilControl : controlInterval;
    }

private:
    LivePitchTracker::Result trackedResult() const noexcept
    {
        // The short-window tracker is authoritative wherever it can see the
        // note (>= 1.15x its 100 Hz floor): it acquires onsets and legato
        // changes in about half the time of the 42.7 ms full-range frame, and
        // never hands over to a second, differently biased first estimate a
        // few ms into a note. The full-range tracker owns the bass register,
        // where only its long frame contains two periods.
        const auto full = tracker.getResult();
        const auto fast = fastTracker.getResult();
        if (full.voiced && full.frequencyHz < 1.15 * fastTracker.getMinimumHz())
            return full;
        return fast.voiced ? fast : full;
    }

    int advance (const float* input, float* output, int available) noexcept
    {
        const int count = std::min (samplesUntilControl, available);
        // Trackers read the input before the shifter may overwrite it in place.
        tracker.process (input, count);
        fastTracker.process (input, count);
        shifter.render (input, output, count);
        samplesUntilControl -= count;
        return count;
    }

    LivePitchTracker tracker;
    LivePitchTracker fastTracker;
    ExtremePitchShifter shifter;
    LiveCorrectionPlanner planner;
    double sr = 48000.0;
    double fallbackPeriod = 266.7;
    LivePitchTracker::Result correctionResult;
    int controlInterval = 32, samplesUntilControl = 0;
    double periodSmoothing = 0.2584;
};

class LivePitchEngine
{
public:
    void prepare (double sampleRate, int maximumBlockSize,
                  double minimumHz = 100.0, double maximumHz = 1100.0)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        tracker.prepare (sampleRate, minimumHz, maximumHz, 256);
        shifter.prepare (sampleRate, minimumHz, maximumHz, maximumBlockSize);
        planner.prepare (sampleRate);
        fallbackPeriod = sampleRate / 180.0;
    }

    void reset() noexcept
    {
        tracker.reset();
        shifter.reset();
        planner.reset();
        correctionResult = {};
    }

    void process (const float* input, float* output, int numSamples,
                  double pitchScale) noexcept
    {
        const auto result = tracker.process (input, numSamples);
        correctionResult = result;
        if (result.voiced && result.periodSamples > 0.0)
            fallbackPeriod = result.periodSamples;
        shifter.process (input, output, numSamples, pitchScale,
                         fallbackPeriod, result.voiced);
    }

    void processCorrected (const float* input, float* output, int numSamples,
                           const LiveCorrectionPlanner::Settings& settings,
                           const LivePitchTracker::Result* pitchGuide = nullptr) noexcept
    {
        const auto tracked = tracker.process (input, numSamples);
        const bool guideValid = pitchGuide != nullptr && pitchGuide->voiced
                             && pitchGuide->frequencyHz > 0.0;
        correctionResult = guideValid ? *pitchGuide : tracked;

        // The waveform tracker remains the preferred epoch period because it is
        // phase-coherent with this exact channel. Neural/fused guidance may steer
        // the musical target and can rescue a dropout; it supplies a period only
        // when the causal tracker has none.
        if (tracked.voiced && tracked.periodSamples > 0.0)
            fallbackPeriod = tracked.periodSamples;
        else if (guideValid)
            fallbackPeriod = pitchGuide->periodSamples > 0.0
                ? pitchGuide->periodSamples : sr / pitchGuide->frequencyHz;

        const double scale = planner.update (correctionResult, numSamples, settings);
        shifter.process (input, output, numSamples, scale,
                         fallbackPeriod, tracked.voiced || guideValid);
    }

    int getLatencySamples() const noexcept { return shifter.getLatencySamples(); }
    LivePitchTracker::Result getPitchResult() const noexcept { return correctionResult; }
    int getTargetMidi() const noexcept { return planner.getTargetMidi(); }
    double getCorrectionCents() const noexcept { return planner.getCorrectionCents(); }

private:
    LivePitchTracker tracker;
    LiveEpochShifter shifter;
    LiveCorrectionPlanner planner;
    double fallbackPeriod = 266.7;
    double sr = 48000.0;
    LivePitchTracker::Result correctionResult;
};

} // namespace sattari::dsp
