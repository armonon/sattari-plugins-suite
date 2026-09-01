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
        const float* const frame = linear.data();
        for (int tau = 1; tau <= maxLag; ++tau)
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
    }

    int getLatencySamples() const noexcept { return latency; }

    void process (const float* input, float* output, int numSamples,
                  double pitchScale, bool voiced,
                  double sourcePeriodSamples = 0.0) noexcept
    {
        if (input == nullptr || output == nullptr || numSamples <= 0 || mask <= 0)
            return;

        constexpr double twoPi = 6.283185307179586476925286766559;
        const double wantedRatio = std::clamp (pitchScale, 0.5, 2.0);
        const double correctionCents = 1200.0 * std::log2 (
            std::max (wantedRatio, 1.0e-12));
        const double targetWet = voiced && std::abs (correctionCents) > 0.10 ? 1.0 : 0.0;
        const bool validPeriod = voiced && sourcePeriodSamples >= 24.0
                              && sourcePeriodSamples <= 0.025 * sr;
        if (validPeriod)
        {
            const double periodRatio = std::clamp (
                sourcePeriodSamples / smoothedPeriod, 0.8, 1.25);
            smoothedPeriod *= std::pow (periodRatio, 0.45);
        }
        else
            hasCycle = false;

        for (int i = 0; i < numSamples; ++i)
        {
            smoothedRatio += ratioCoefficient * (wantedRatio - smoothedRatio);

            // A causal anti-alias guard matters only for upward shifts. Near the
            // correction range (normally < one semitone) its cutoff stays above
            // 20 kHz at 48 kHz; octave harmony lowers it before resampling rather
            // than folding upper vocal harmonics into the audible band.
            const double cutoff = std::min (0.45 * sr, 0.45 * sr / std::max (1.0, smoothedRatio));
            const double lp = 1.0 - std::exp (-twoPi * cutoff / sr);
            antiAlias1 += lp * ((double) input[i] - antiAlias1);
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

            wetMix += wetCoefficient * (targetWet - wetMix);
            output[i] = (float) ((double) dry + wetMix * (shifted - (double) dry));
            ++writePosition;
        }
    }

private:
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
            if (hasCycle)
            {
                double cross = 0.0, oldPower = 1.0e-12, newPower = 1.0e-12;
                for (int k = -12; k <= 12; ++k)
                {
                    const double oldSample = sampleAt (currentCycleStart + k);
                    const double newSample = sampleAt (p + k);
                    cross += oldSample * newSample;
                    oldPower += oldSample * oldSample;
                    newPower += newSample * newSample;
                }
                score = cross / std::sqrt (oldPower * newPower);
            }
            else
            {
                const double x = sampleAt (p);
                const double derivative = x - 0.97 * (double) sampleAt (p - 1);
                const double curvature = x - 0.5 * ((double) sampleAt (p - 1)
                                                    + (double) sampleAt (p + 1));
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
    std::int64_t currentCycleStart = 0, previousCycleStart = 0;
    bool hasCycle = false;
    double wetCoefficient = 0.01, ratioCoefficient = 0.02;
    double antiAlias1 = 0.0, antiAlias2 = 0.0;
    std::vector<float> ring, dryRing;
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
        // window for ordinary vocal notes; the full tracker takes authority as
        // soon as it is voiced, preserving the advertised bass range.
        fastTracker.prepare (sr, std::max (100.0, minimumHz), maximumHz, 128);
        shifter.prepare (sr, maximumBlockSize);
        planner.prepare (sr);
    }

    void reset() noexcept
    {
        tracker.reset();
        fastTracker.reset();
        shifter.reset();
        planner.reset();
        fallbackPeriod = sr / 180.0;
        correctionResult = {};
    }

    void process (const float* input, float* output, int numSamples,
                  double pitchScale) noexcept
    {
        const auto fullResult = tracker.process (input, numSamples);
        const auto fastResult = fastTracker.process (input, numSamples);
        const auto result = fullResult.voiced ? fullResult : fastResult;
        correctionResult = result;
        if (result.voiced && result.periodSamples > 0.0)
            fallbackPeriod = result.periodSamples;
        shifter.process (input, output, numSamples, pitchScale, result.voiced,
                         fallbackPeriod);
    }

    void processCorrected (const float* input, float* output, int numSamples,
                           const LiveCorrectionPlanner::Settings& settings,
                           const LivePitchTracker::Result* pitchGuide = nullptr) noexcept
    {
        const auto fullResult = tracker.process (input, numSamples);
        const auto fastResult = fastTracker.process (input, numSamples);
        const auto tracked = fullResult.voiced ? fullResult : fastResult;
        const bool guideValid = pitchGuide != nullptr && pitchGuide->voiced
                             && pitchGuide->frequencyHz > 0.0;
        correctionResult = guideValid ? *pitchGuide : tracked;
        if (tracked.voiced && tracked.periodSamples > 0.0)
            fallbackPeriod = tracked.periodSamples;
        else if (guideValid)
            fallbackPeriod = pitchGuide->periodSamples > 0.0
                ? pitchGuide->periodSamples : sr / pitchGuide->frequencyHz;
        const double scale = planner.update (correctionResult, numSamples, settings);
        shifter.process (input, output, numSamples, scale, tracked.voiced || guideValid,
                         fallbackPeriod);
    }

    int getLatencySamples() const noexcept { return shifter.getLatencySamples(); }
    LivePitchTracker::Result getPitchResult() const noexcept { return correctionResult; }
    int getTargetMidi() const noexcept { return planner.getTargetMidi(); }
    double getCorrectionCents() const noexcept { return planner.getCorrectionCents(); }

private:
    LivePitchTracker tracker;
    LivePitchTracker fastTracker;
    ExtremePitchShifter shifter;
    LiveCorrectionPlanner planner;
    double sr = 48000.0;
    double fallbackPeriod = 266.7;
    LivePitchTracker::Result correctionResult;
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
