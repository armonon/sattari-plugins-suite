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
                if (filled == length)
                    analyseFrame();
            }
        }
        return latest;
    }

    Result getResult() const noexcept { return latest; }
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

        // Fixed comparison length gives every lag the same statistical weight.
        const int compare = length - maxLag - 1;
        yin[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double difference = 0.0;
            for (int i = 0; i < compare; ++i)
            {
                const double d = (double) linear[(size_t) i]
                               - (double) linear[(size_t) (i + tau)];
                difference += d * d;
            }
            running += difference;
            yin[(size_t) tau] = (float) (running > 1.0e-18
                ? difference * (double) tau / running : 1.0);
        }

        constexpr float threshold = 0.16f;
        int best = -1;
        for (int tau = minLag; tau < maxLag; ++tau)
        {
            if (yin[(size_t) tau] < threshold)
            {
                while (tau + 1 <= maxLag && yin[(size_t) (tau + 1)] < yin[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            float score = std::numeric_limits<float>::max();
            for (int tau = minLag; tau <= maxLag; ++tau)
                if (yin[(size_t) tau] < score) { score = yin[(size_t) tau]; best = tau; }
        }

        const float confidence = best >= minLag
            ? std::clamp (1.0f - yin[(size_t) best], 0.0f, 1.0f) : 0.0f;
        // Entry remains stricter than hold so breaths cannot chatter the voiced
        // switch. The values are deliberately below clean-sine confidence: real
        // close-mic vocals with saturation or room energy commonly sit around
        // 0.45-0.70 even when their fundamental is musically unambiguous.
        const float required = latest.voiced ? 0.42f : 0.60f;
        if (best < minLag || confidence < required)
        {
            latest.confidence = confidence;
            acceptUnvoiced();
            return;
        }

        double lag = (double) best;
        if (best > minLag && best < maxLag)
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

        const double midi = 69.0 + 12.0 * std::log2 (pitch.frequencyHz / 440.0);
        if (! voiced)
        {
            centreMidi = midi;
            correctionCents = 0.0;
            targetMidi = nearestEnabledMidi (midi, settings.enabledNotes);
            voiced = true;
        }

        // A real note transition must not be mistaken for huge vibrato around the
        // old note. Re-anchor before the slow centre filter can ask the shifter to
        // drag a new syllable back by several semitones. Ordinary vibrato remains
        // below this threshold and therefore keeps its continuous centre.
        if (std::abs (midi - centreMidi) > 0.80)
        {
            centreMidi = midi;
            targetMidi = nearestEnabledMidi (midi, settings.enabledNotes);
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
        const int candidate = nearestEnabledMidi (midi, settings.enabledNotes);
        if (targetMidi < 0)
            targetMidi = candidate;
        else if (candidate != targetMidi)
        {
            const double currentDistance = std::abs (midi - (double) targetMidi);
            const double candidateDistance = std::abs (midi - (double) candidate);
            if (candidateDistance + std::max (0.0, settings.noteHysteresisCents) / 100.0
                < currentDistance)
                targetMidi = candidate;
        }

        const double residualSemitones = midi - centreMidi;
        const double preserve = std::clamp (settings.preserveVibrato, 0.0, 1.0);
        const double desiredSemitones = (double) targetMidi - centreMidi
                                      - (1.0 - preserve) * residualSemitones;
        const double desiredCents = 100.0 * std::clamp (settings.amount, 0.0, 1.0)
                                  * desiredSemitones;

        // Even the Hard setting gets a 1.5 ms dezipper. It is short enough to
        // feel immediate and prevents a parameter/note boundary from making a
        // discontinuous pitch-scale step.
        const double seconds = std::max (0.0015, 0.001 * settings.retuneMilliseconds);
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

class LivePitchEngine
{
public:
    void prepare (double sampleRate, int maximumBlockSize,
                  double minimumHz = 100.0, double maximumHz = 1100.0)
    {
        tracker.prepare (sampleRate, minimumHz, maximumHz, 256);
        shifter.prepare (sampleRate, minimumHz, maximumHz, maximumBlockSize);
        planner.prepare (sampleRate);
        fallbackPeriod = sampleRate / 180.0;
    }

    void reset() noexcept { tracker.reset(); shifter.reset(); planner.reset(); }

    void process (const float* input, float* output, int numSamples,
                  double pitchScale) noexcept
    {
        const auto result = tracker.process (input, numSamples);
        if (result.voiced && result.periodSamples > 0.0)
            fallbackPeriod = result.periodSamples;
        shifter.process (input, output, numSamples, pitchScale,
                         fallbackPeriod, result.voiced);
    }

    void processCorrected (const float* input, float* output, int numSamples,
                           const LiveCorrectionPlanner::Settings& settings) noexcept
    {
        const auto result = tracker.process (input, numSamples);
        if (result.voiced && result.periodSamples > 0.0)
            fallbackPeriod = result.periodSamples;
        const double scale = planner.update (result, numSamples, settings);
        shifter.process (input, output, numSamples, scale,
                         fallbackPeriod, result.voiced);
    }

    int getLatencySamples() const noexcept { return shifter.getLatencySamples(); }
    LivePitchTracker::Result getPitchResult() const noexcept { return tracker.getResult(); }
    int getTargetMidi() const noexcept { return planner.getTargetMidi(); }
    double getCorrectionCents() const noexcept { return planner.getCorrectionCents(); }

private:
    LivePitchTracker tracker;
    LiveEpochShifter shifter;
    LiveCorrectionPlanner planner;
    double fallbackPeriod = 266.7;
};

} // namespace sattari::dsp
