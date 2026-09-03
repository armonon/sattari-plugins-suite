#pragma once

// =============================================================================
//  Sattari Dynamic Key Engine v2
// -----------------------------------------------------------------------------
//  A causal 24-state (12 roots x major/minor) tracker for music whose key can
//  change during playback. It does not try to infer key from a single exposed
//  chord. Every one-second observation fuses four independent cues:
//
//    * full-band HPCP profile fit;
//    * the release detector's multi-resolution verdict;
//    * low-frequency tonic/fifth evidence;
//    * beat/onset-weighted chord-root evidence.
//
//  A short HMM-like decoder then favours musically plausible state transitions
//  without making any transition impossible. The fast lane may establish a
//  clean initial key after one observation and a tonic-confirmed change after
//  two. Everything else must remain consistent for the selected 1-5 second
//  stability horizon. Silence, drums,
//  low-margin guesses and isolated chords retain the last trusted key.
//
//  This class is deliberately independent of plugin state and wall-clock time:
//  callers feed one observation per second, which makes the safety contract
//  deterministic and directly testable.
// =============================================================================

#include <sattari_audio_core/SattariChordAnalysis.h>
#include <sattari_audio_core/SattariKeyAnalysis.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace sattari::dsp
{

struct DynamicKeyObservation
{
    KeyAnalysisResult detector;
    std::array<double, 12> chroma {};
    std::array<double, 12> bassChroma {};
    std::array<double, 12> chordRootEvidence {};
    ChordResult summaryChord;
    double rms = 0.0;
};

struct DynamicKeyDecision
{
    bool valid = false;
    int root = 0;
    bool minor = false;
    double confidence = 0.0;
    int revision = 0;
    bool changed = false;
    bool fastLane = false;

    bool pending = false;
    int pendingRoot = 0;
    bool pendingMinor = false;
    int pendingConfirmations = 0;
    int requiredConfirmations = 0;
    int evidenceWindows = 0;
};

class DynamicKeyTracker
{
public:
    static constexpr int kStates = 24;
    static constexpr int kMaxHistory = 5;
    static constexpr double kMinimumRms = 1.0e-4;
    static constexpr double kMinimumObservationConfidence = 0.06;
    static constexpr double kTrustedConfidence = kReleaseKeyConfidence;
    static constexpr double kFastConfidence = 0.22;

    void reset() noexcept
    {
        for (auto& h : history_) h.fill (0.0);
        path_.fill (0.0);
        historyHead_ = historyCount_ = 0;
        pathValid_ = false;
        acceptedValid_ = false;
        acceptedState_ = 0;
        acceptedConfidence_ = 0.0;
        revision_ = 0;
        pendingState_ = -1;
        pendingCount_ = 0;
    }

    DynamicKeyDecision update (const DynamicKeyObservation& observation,
                               double stabilityHorizonSeconds) noexcept
    {
        DynamicKeyDecision decision = snapshot();
        decision.changed = false;
        decision.fastLane = false;
        decision.requiredConfirmations = requiredFor (stabilityHorizonSeconds);

        if (! observationIsUsable (observation))
        {
            // A gap is not contradictory evidence. Preserve the accepted key,
            // but require a candidate to start a fresh uninterrupted run.
            pendingState_ = -1;
            pendingCount_ = 0;
            return snapshotWithRequirements (decision.requiredConfirmations);
        }

        const auto emission = makeEmission (observation);
        history_[(size_t) historyHead_] = emission;
        historyHead_ = (historyHead_ + 1) % kMaxHistory;
        historyCount_ = std::min (kMaxHistory, historyCount_ + 1);

        const int required = decision.requiredConfirmations;
        const auto stable = stableEvidence (required);
        std::array<double, kStates> observationScore {};
        for (int state = 0; state < kStates; ++state)
            observationScore[(size_t) state]
                = 0.58 * emission[(size_t) state]
                + 0.42 * stable[(size_t) state];

        std::array<double, kStates> nextPath {};
        if (! pathValid_)
        {
            nextPath = observationScore;
            pathValid_ = true;
        }
        else
        {
            for (int to = 0; to < kStates; ++to)
            {
                double bestPrevious = -10.0;
                for (int from = 0; from < kStates; ++from)
                    bestPrevious = std::max (
                        bestPrevious,
                        0.70 * path_[(size_t) from] - transitionCost (from, to));
                nextPath[(size_t) to] = observationScore[(size_t) to] + bestPrevious;
            }
        }

        const double maximum = *std::max_element (nextPath.begin(), nextPath.end());
        for (auto& score : nextPath) score -= maximum;
        path_ = nextPath;

        int bestState = 0;
        double best = -10.0, second = -10.0;
        for (int state = 0; state < kStates; ++state)
        {
            const double score = nextPath[(size_t) state];
            if (score > best) { second = best; best = score; bestState = state; }
            else if (score > second) second = score;
        }

        // The decoder is allowed to resolve a relative-key ambiguity, but it
        // may not overturn a confident detector with unrelated weak evidence.
        const int detectorState = stateFor (
            observation.detector.root, observation.detector.minor);
        if (bestState != detectorState
            && ! relatedStates (bestState, detectorState)
            && observation.detector.confidence >= kTrustedConfidence)
            bestState = detectorState;

        const double decodedMargin = std::clamp (best - second, 0.0, 1.0);
        const double confidence = calibratedConfidence (
            observation, bestState, decodedMargin);
        if (confidence < kTrustedConfidence)
        {
            pendingState_ = -1;
            pendingCount_ = 0;
            return snapshotWithRequirements (required);
        }

        const bool sameAsAccepted = acceptedValid_ && bestState == acceptedState_;
        if (sameAsAccepted)
        {
            acceptedConfidence_ = confidence;
            pendingState_ = -1;
            pendingCount_ = 0;
            return snapshotWithRequirements (required);
        }

        if (pendingState_ == bestState)
            ++pendingCount_;
        else
        {
            pendingState_ = bestState;
            pendingCount_ = 1;
        }

        // Once a singer is being corrected, one chord must never retune them.
        // Even fast-lane changes therefore need two adjacent one-second votes;
        // the first trusted acquisition may enter immediately.
        const bool fast = fastLaneIsSafe (observation, bestState)
            && pendingCount_ >= (acceptedValid_ ? 2 : 1);
        const int initialRequired = std::min (2, required);
        if (fast || pendingCount_ >= (acceptedValid_ ? required : initialRequired))
        {
            acceptedValid_ = true;
            acceptedState_ = bestState;
            acceptedConfidence_ = confidence;
            ++revision_;
            pendingState_ = -1;
            pendingCount_ = 0;
            auto accepted = snapshotWithRequirements (required);
            accepted.changed = true;
            accepted.fastLane = fast;
            return accepted;
        }

        return snapshotWithRequirements (required);
    }

    DynamicKeyDecision decision() const noexcept { return snapshot(); }

private:
    static int stateFor (int root, bool minor) noexcept
    {
        const int wrapped = ((root % 12) + 12) % 12;
        return wrapped * 2 + (minor ? 1 : 0);
    }

    static int rootFor (int state) noexcept { return state / 2; }
    static bool minorFor (int state) noexcept { return (state & 1) != 0; }

    static int requiredFor (double seconds) noexcept
    {
        return std::clamp ((int) std::lround (seconds), 1, kMaxHistory);
    }

    static double sumPositive (const std::array<double, 12>& values) noexcept
    {
        double sum = 0.0;
        for (const auto value : values) sum += std::max (0.0, value);
        return sum;
    }

    static bool observationIsUsable (const DynamicKeyObservation& o) noexcept
    {
        return o.rms >= kMinimumRms
            && o.detector.valid
            && o.detector.confidence >= kMinimumObservationConfidence
            && sumPositive (o.chroma) > 1.0e-9;
    }

    static bool relatedStates (int a, int b) noexcept
    {
        const int ar = rootFor (a), br = rootFor (b);
        const bool am = minorFor (a), bm = minorFor (b);
        if (ar == br) return true; // parallel
        if (am == bm && ((ar - br + 12) % 12 == 7
                      || (br - ar + 12) % 12 == 7)) return true; // fifth
        return am != bm && ((! am && ar == (br + 3) % 12)
                         || (am && br == (ar + 3) % 12)); // relative
    }

    static double transitionCost (int from, int to) noexcept
    {
        if (from == to) return 0.0;
        const int fr = rootFor (from), tr = rootFor (to);
        const bool fm = minorFor (from), tm = minorFor (to);
        if (fm != tm && ((! fm && fr == (tr + 3) % 12)
                      || (fm && tr == (fr + 3) % 12))) return 0.045;
        if (fm == tm && ((fr - tr + 12) % 12 == 7
                      || (tr - fr + 12) % 12 == 7)) return 0.070;
        if (fr == tr) return 0.090;
        const int distance = std::min ((fr - tr + 12) % 12,
                                       (tr - fr + 12) % 12);
        return 0.15 + 0.012 * (double) distance;
    }

    static std::array<double, kStates> makeEmission (
        const DynamicKeyObservation& o) noexcept
    {
        std::array<double, kStates> out {};
        const double bassTotal = sumPositive (o.bassChroma);
        const double chordTotal = sumPositive (o.chordRootEvidence);
        const int detectorState = stateFor (o.detector.root, o.detector.minor);
        const double detectorWeight = std::clamp (
            o.detector.confidence / 0.30, 0.0, 1.0);

        for (int state = 0; state < kStates; ++state)
        {
            const int root = rootFor (state);
            const bool minor = minorFor (state);
            const auto* profile = minor ? detail::kAardenMinor
                                        : detail::kAardenMajor;
            const double profileFit = 0.5 * (detail::pearson (
                o.chroma.data(), profile, root) + 1.0);

            double bass = 0.0;
            if (bassTotal > 1.0e-12)
            {
                bass += 0.72 * std::max (0.0, o.bassChroma[(size_t) root]) / bassTotal;
                bass += 0.20 * std::max (0.0, o.bassChroma[(size_t) ((root + 7) % 12)]) / bassTotal;
                bass += 0.08 * std::max (0.0, o.bassChroma[(size_t) ((root + (minor ? 3 : 4)) % 12)]) / bassTotal;
            }

            double chord = 0.0;
            if (chordTotal > 1.0e-12)
            {
                for (int chordRoot = 0; chordRoot < 12; ++chordRoot)
                {
                    const double e = std::max (
                        0.0, o.chordRootEvidence[(size_t) chordRoot]) / chordTotal;
                    const int relative = (chordRoot - root + 12) % 12;
                    bool scaleTone = false;
                    static constexpr int majorDegrees[7] = { 0, 2, 4, 5, 7, 9, 11 };
                    static constexpr int minorDegrees[7] = { 0, 2, 3, 5, 7, 8, 10 };
                    const int* degrees = minor ? minorDegrees : majorDegrees;
                    for (int i = 0; i < 7; ++i)
                        if (relative == degrees[i]) { scaleTone = true; break; }
                    if (scaleTone) chord += 0.45 * e;
                    if (relative == 0) chord += 0.55 * e;
                    else if (relative == 7) chord += 0.30 * e;
                    else if (relative == 5) chord += 0.22 * e;
                }
            }
            else if (o.summaryChord.valid)
            {
                const double chordTrust = std::clamp (
                    o.summaryChord.confidence * 4.0, 0.0, 1.0);
                if (ChordAnalyzer::chordIsDiatonic (
                        o.summaryChord.root, o.summaryChord.quality, root, minor))
                    chord += 0.45 * chordTrust;
                const int relative = (o.summaryChord.root - root + 12) % 12;
                if (relative == 0) chord += 0.45 * chordTrust;
                else if (relative == 7) chord += 0.20 * chordTrust;
            }

            const double detector = state == detectorState ? detectorWeight : 0.0;
            out[(size_t) state] = 0.58 * profileFit
                                + 0.16 * bass
                                + 0.14 * chord
                                + 0.12 * detector;
        }
        return out;
    }

    std::array<double, kStates> stableEvidence (int requested) const noexcept
    {
        std::array<double, kStates> stable {};
        const int count = std::min (historyCount_, requested);
        double totalWeight = 0.0;
        for (int age = 0; age < count; ++age)
        {
            const int index = (historyHead_ - 1 - age + kMaxHistory) % kMaxHistory;
            const double weight = 1.0 - 0.08 * (double) age;
            totalWeight += weight;
            for (int state = 0; state < kStates; ++state)
                stable[(size_t) state] += weight * history_[(size_t) index][(size_t) state];
        }
        if (totalWeight > 0.0)
            for (auto& score : stable) score /= totalWeight;
        return stable;
    }

    static double calibratedConfidence (const DynamicKeyObservation& o,
                                        int decodedState,
                                        double decodedMargin) noexcept
    {
        // The detector margin remains the dominant, honest confidence source.
        // Temporal agreement can support it, never inflate it by more than 20%.
        const int detectorState = stateFor (o.detector.root, o.detector.minor);
        const double agreement = decodedState == detectorState ? 1.0
                               : relatedStates (decodedState, detectorState) ? 0.72
                               : 0.35;
        const double supported = o.detector.confidence * agreement
            + std::min (0.20 * o.detector.confidence, 0.25 * decodedMargin);
        return std::clamp (supported, 0.0, 1.0);
    }

    static bool fastLaneIsSafe (const DynamicKeyObservation& o,
                                int decodedState) noexcept
    {
        const int root = rootFor (decodedState);
        if (decodedState != stateFor (o.detector.root, o.detector.minor)
            || o.detector.confidence < kFastConfidence)
            return false;

        const double bassTotal = sumPositive (o.bassChroma);
        const double tonicBass = bassTotal > 1.0e-12
            ? std::max (0.0, o.bassChroma[(size_t) root]) / bassTotal : 0.0;
        const double chordTotal = sumPositive (o.chordRootEvidence);
        const double tonicChord = chordTotal > 1.0e-12
            ? std::max (0.0, o.chordRootEvidence[(size_t) root]) / chordTotal : 0.0;
        const double supportingChords = chordTotal > 1.0e-12
            ? (std::max (0.0, o.chordRootEvidence[(size_t) ((root + 5) % 12)])
             + std::max (0.0, o.chordRootEvidence[(size_t) ((root + 7) % 12)]))
                / chordTotal
            : 0.0;
        const bool summarySupportsTonic = o.summaryChord.valid
            && o.summaryChord.root == root
            && o.summaryChord.confidence >= 0.025;

        double chromaTotal = 0.0, chromaPeak = 0.0, entropy = 0.0;
        for (const auto value : o.chroma)
        {
            chromaTotal += std::max (0.0, value);
            chromaPeak = std::max (chromaPeak, std::max (0.0, value));
        }
        int coveredPitchClasses = 0;
        if (chromaTotal > 1.0e-12)
        {
            for (const auto value : o.chroma)
            {
                const double normalised = std::max (0.0, value) / chromaTotal;
                if (normalised > 1.0e-12)
                    entropy -= normalised * std::log (normalised);
                if (value >= std::max (0.025 * chromaTotal, 0.18 * chromaPeak))
                    ++coveredPitchClasses;
            }
            entropy /= std::log (12.0);
        }

        // Requiring both a tonic-bearing low end and harmonic confirmation stops
        // the fast lane from following every chord in an ordinary progression.
        // Scale breadth is equally important: a clean triad can strongly match a
        // key profile but it does not yet establish a modulation.
        return tonicBass >= 0.16
            && (tonicChord >= 0.24 || summarySupportsTonic)
            && supportingChords >= 0.16
            && coveredPitchClasses >= 5
            && entropy >= 0.58;
    }

    DynamicKeyDecision snapshot() const noexcept
    {
        DynamicKeyDecision d;
        d.valid = acceptedValid_;
        if (acceptedValid_)
        {
            d.root = rootFor (acceptedState_);
            d.minor = minorFor (acceptedState_);
            d.confidence = acceptedConfidence_;
        }
        d.revision = revision_;
        d.pending = pendingState_ >= 0;
        if (d.pending)
        {
            d.pendingRoot = rootFor (pendingState_);
            d.pendingMinor = minorFor (pendingState_);
            d.pendingConfirmations = pendingCount_;
        }
        d.evidenceWindows = historyCount_;
        return d;
    }

    DynamicKeyDecision snapshotWithRequirements (int required) const noexcept
    {
        auto d = snapshot();
        d.requiredConfirmations = required;
        return d;
    }

    std::array<std::array<double, kStates>, kMaxHistory> history_ {};
    std::array<double, kStates> path_ {};
    int historyHead_ = 0;
    int historyCount_ = 0;
    bool pathValid_ = false;

    bool acceptedValid_ = false;
    int acceptedState_ = 0;
    double acceptedConfidence_ = 0.0;
    int revision_ = 0;
    int pendingState_ = -1;
    int pendingCount_ = 0;
};

} // namespace sattari::dsp
