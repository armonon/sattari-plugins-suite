// Contract for the causal Dynamic Key Engine v2.
//
// It protects the behaviour that matters when this output drives a singer:
//   * a clean tonic-confirmed key can enter through the one-second fast lane;
//   * normal chords inside a key do not masquerade as modulations;
//   * a marginal change must survive the complete stability horizon;
//   * one-window excursions and silence cannot move the accepted key;
//   * revisions increment only when the accepted musical state changes.
#include "sattari_audio_core/SattariDynamicKeyTracker.h"

#include <array>
#include <cstdio>

namespace
{
int failures = 0;

void expect (bool condition, const char* label)
{
    std::printf ("  %-46s %s\n", label, condition ? "ok" : "FAIL");
    if (! condition) ++failures;
}

sattari::dsp::DynamicKeyObservation observation (
    int root, bool minor, double confidence,
    int bassRoot, int chordRoot, double rms = 0.20)
{
    sattari::dsp::DynamicKeyObservation o;
    o.detector.valid = true;
    o.detector.root = root;
    o.detector.minor = minor;
    o.detector.confidence = confidence;
    o.detector.source = sattari::dsp::KeyAnalysisSource::Chroma;
    o.rms = rms;

    const auto* profile = minor
        ? sattari::dsp::detail::kAardenMinor
        : sattari::dsp::detail::kAardenMajor;
    for (int i = 0; i < 12; ++i)
        o.chroma[(size_t) ((i + root) % 12)] = profile[i];

    o.bassChroma[(size_t) bassRoot] = 1.0;
    o.bassChroma[(size_t) ((bassRoot + 7) % 12)] = 0.22;
    o.chordRootEvidence[(size_t) chordRoot] = 1.0;
    o.summaryChord.valid = true;
    o.summaryChord.root = chordRoot;
    o.summaryChord.quality = minor
        ? sattari::dsp::ChordQuality::minor
        : sattari::dsp::ChordQuality::major;
    o.summaryChord.confidence = 0.10;
    return o;
}
} // namespace

int main()
{
    using sattari::dsp::DynamicKeyTracker;
    std::printf ("dynamic key tracker contract\n");

    DynamicKeyTracker tracker;

    // Clean C tonic: strong detector + tonic bass + tonic chord earns fast lane.
    auto cleanC = observation (0, false, 0.26, 0, 0);
    cleanC.chordRootEvidence[5] = 0.25;
    cleanC.chordRootEvidence[7] = 0.30;
    auto d = tracker.update (cleanC, 3.0);
    expect (d.valid && d.root == 0 && ! d.minor, "clean C major accepted");
    expect (d.changed && d.fastLane && d.revision == 1,
            "tonic-confirmed entrance uses fast lane");

    {
        DynamicKeyTracker fastChangeTracker;
        (void) fastChangeTracker.update (cleanC, 5.0);
        auto cleanG = observation (7, false, 0.28, 7, 7);
        cleanG.chordRootEvidence[0] = 0.25;
        cleanG.chordRootEvidence[2] = 0.30;
        auto first = fastChangeTracker.update (cleanG, 5.0);
        auto second = fastChangeTracker.update (cleanG, 5.0);
        expect (first.root == 0 && first.pending && first.revision == 1,
                "fast modulation still requires adjacent evidence");
        expect (second.root == 7 && second.fastLane && second.revision == 2,
                "context-rich modulation enters fast lane at 2 s");
    }

    // IV and V are chord movement, not key movement. The key observation remains C.
    d = tracker.update (observation (0, false, 0.14, 5, 5), 3.0);
    d = tracker.update (observation (0, false, 0.13, 7, 7), 3.0);
    expect (d.valid && d.root == 0 && d.revision == 1,
            "in-key chord progression keeps C major");

    // A single marginal, unrelated E-major window cannot retune the suite.
    d = tracker.update (observation (4, false, 0.13, 4, 4), 3.0);
    expect (d.valid && d.root == 0 && d.revision == 1 && d.pending,
            "single marginal excursion remains pending");
    d = tracker.update (observation (0, false, 0.14, 0, 0), 3.0);
    expect (d.root == 0 && d.revision == 1 && ! d.pending,
            "return to accepted key clears excursion");

    // Even a high detector margin is not enough for a one-chord modulation.
    // The fast lane also requires scale breadth, not just tonic-like evidence.
    auto isolatedChord = observation (5, false, 0.32, 5, 5);
    isolatedChord.chroma.fill (0.0);
    isolatedChord.chroma[5] = 1.0;
    isolatedChord.chroma[9] = 0.85;
    isolatedChord.chroma[0] = 0.90;
    d = tracker.update (isolatedChord, 3.0);
    expect (d.root == 0 && d.revision == 1 && d.pending && ! d.fastLane,
            "strong isolated chord cannot use fast lane");
    d = tracker.update (observation (0, false, 0.14, 0, 0), 3.0);
    expect (d.root == 0 && d.revision == 1 && ! d.pending,
            "isolated chord does not become a modulation");

    // A marginal D change needs the complete three-window stable lane.
    d = tracker.update (observation (2, false, 0.14, 2, 2), 3.0);
    expect (d.root == 0 && d.pending && d.pendingConfirmations == 1,
            "stable change window 1 of 3");
    d = tracker.update (observation (2, false, 0.14, 2, 2), 3.0);
    expect (d.root == 0 && d.pending && d.pendingConfirmations == 2,
            "stable change window 2 of 3");
    d = tracker.update (observation (2, false, 0.14, 2, 2), 3.0);
    expect (d.root == 2 && ! d.minor && d.changed && ! d.fastLane
            && d.revision == 2,
            "stable change accepted only at window 3");

    // Silence is a hold state, never evidence for C or any other default key.
    auto silence = observation (0, false, 0.90, 0, 0, 0.0);
    silence.chroma.fill (0.0);
    silence.bassChroma.fill (0.0);
    silence.chordRootEvidence.fill (0.0);
    silence.summaryChord = {};
    d = tracker.update (silence, 3.0);
    expect (d.valid && d.root == 2 && d.revision == 2 && ! d.changed,
            "silence holds last trusted key");

    // Below-gate tonal guesses also hold and do not create pending UI state.
    d = tracker.update (observation (9, true, 0.04, 9, 9), 3.0);
    expect (d.root == 2 && d.revision == 2 && ! d.pending,
            "low-margin guess is rejected");

    std::printf (failures == 0 ? "dynamic key tracker contract ok\n"
                               : "dynamic key tracker contract FAILED\n");
    return failures == 0 ? 0 : 1;
}
