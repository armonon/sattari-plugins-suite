#include "BeatGridDecoder.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace
{
int failures = 0;

void check (bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void addPulse (std::vector<float>& activations, int frame, float strength = 1.0f)
{
    if (frame < 1 || frame + 1 >= (int) activations.size()) return;
    activations[(size_t) (frame - 1)] = std::max (activations[(size_t) (frame - 1)], strength * 0.2f);
    activations[(size_t) frame] = std::max (activations[(size_t) frame], strength);
    activations[(size_t) (frame + 1)] = std::max (activations[(size_t) (frame + 1)], strength * 0.25f);
}
}

int main()
{
    constexpr double fps = 50.0;

    // Ten seconds of ambient intro followed by an obvious 120-BPM section. The
    // decoder must infer the early grid instead of beginning at the first peak.
    std::vector<float> ambient (1500, 0.01f);
    for (int frame = 500; frame < (int) ambient.size(); frame += 25)
        addPulse (ambient, frame);
    const auto intro = stemdeck::BeatGridDecoder::decode (ambient, fps);
    check ((bool) intro, "ambient-intro grid is valid");
    check (! intro.frames.empty() && intro.frames.front() < 50,
           "grid is propagated through ambient intro");
    check (std::abs (intro.bpm - 120.0) < 3.0, "ambient-intro tempo remains 120 BPM");

    // A five-second breakdown has no beat activations at all. Grid spacing must
    // remain continuous across it, not fragment into two unrelated peak groups.
    std::vector<float> breakdown (1200, 0.005f);
    for (int frame = 0; frame < (int) breakdown.size(); frame += 25)
        if (frame < 400 || frame > 650)
            addPulse (breakdown, frame, 0.9f);
    const auto bridge = stemdeck::BeatGridDecoder::decode (breakdown, fps);
    check ((bool) bridge, "breakdown grid is valid");
    int largestGap = 0;
    for (size_t i = 1; i < bridge.frames.size(); ++i)
        largestGap = std::max (largestGap, bridge.frames[i] - bridge.frames[i - 1]);
    check (largestGap <= 30, "decoder bridges a silent breakdown without dropping beats");

    // Follow a gradual live-drum accelerando (100 -> about 125 BPM) while keeping
    // every transition bounded. A fixed global-period snapper fails this case.
    std::vector<float> drift (1800, 0.005f);
    double at = 10.0;
    while (at < (double) drift.size() - 2.0)
    {
        const double progress = at / (double) drift.size();
        const double period = 30.0 - 6.0 * progress;
        addPulse (drift, (int) std::lround (at), 0.95f);
        at += period;
    }
    const auto moving = stemdeck::BeatGridDecoder::decode (drift, fps);
    check ((bool) moving, "tempo-drift grid is valid");
    int maxChange = 0;
    for (size_t i = 2; i < moving.frames.size(); ++i)
    {
        const int previous = moving.frames[i - 1] - moving.frames[i - 2];
        const int current = moving.frames[i] - moving.frames[i - 1];
        maxChange = std::max (maxChange, std::abs (current - previous));
    }
    check (maxChange <= 4, "tempo-state transitions remain gradual");

    std::vector<float> silence (1000, 0.0f);
    check (! stemdeck::BeatGridDecoder::decode (silence, fps),
           "silence does not produce a fabricated beat grid");

    // ── Confidence must actually discriminate ────────────────────────────────
    //
    // Nothing above asserts on `confidence`, and that is how a real defect shipped: the
    // regularity term compared each interval against the per-state period the backtrack had
    // used to place it, so it was identically 1.0 and confidence reduced to the evidence
    // term alone. Structureless noise scored 0.67 against a 0.35 gate; an irregular
    // wrong-tempo grid outscored a correct quiet one.
    //
    // MainPluginProcessor::kNeuralBeatConfidenceFloor is the gate these feed. Kept as a
    // literal because this target links only the header, not the processor.
    constexpr double kBeatConfidenceFloor = 0.35;

    std::vector<float> clean (1500, 0.01f);
    for (int frame = 25; frame < (int) clean.size(); frame += 25)
        addPulse (clean, frame);
    const auto cleanGrid = stemdeck::BeatGridDecoder::decode (clean, fps);
    check ((bool) cleanGrid, "clean 120 BPM grid is valid");
    check (cleanGrid.confidence > 0.8,
           "a clean, regular grid is reported with high confidence");

    // Same pulse strength, same count — only the SPACING is irregular. Evidence is
    // therefore near-identical and regularity is the only term that can separate them.
    // With the old formula both scored the same; this is the assertion that pins the fix.
    std::vector<float> jittered (1500, 0.01f);
    {
        unsigned state = 12345u;                       // fixed seed: deterministic
        int position = 25;
        while (position < (int) jittered.size() - 2)
        {
            addPulse (jittered, position);
            state = state * 1103515245u + 12345u;
            const int jitter = (int) ((state >> 16) % 21u) - 10;  // -10..+10 on a 25-frame period
            position += 25 + jitter;
        }
    }
    const auto jitteredGrid = stemdeck::BeatGridDecoder::decode (jittered, fps);

    // A MARGIN, not just an ordering. Ordering alone does not catch the original bug: with
    // the dead regularity term the evidence term still happened to rank these correctly
    // (measured 0.971 jittered vs 0.998 clean), so `jittered < clean` passed on the broken
    // code. The regularity term has to visibly CONTRIBUTE.
    //
    // Sizing the margin from the mechanism rather than from the observed number: ±10 frames
    // of jitter on a 25-frame period gives a mean |deviation| / median of about 0.2, so
    // regularity ≈ 1/(1+0.2) ≈ 0.83 — a drop of roughly 17%. Asserting at least 8% is
    // conservatively half the predicted effect, which separates cleanly on both sides
    // (broken 0.971 > 0.918 fails; fixed 0.855 < 0.918 passes).
    check (! jitteredGrid || jitteredGrid.confidence < cleanGrid.confidence * 0.92,
           "an irregularly-spaced grid scores materially below a regular one "
           "(regularity term actually contributes)");

    // Structureless activations: no beat exists, so whatever grid comes back must not be
    // trusted enough to override the measured DSP grid.
    std::vector<float> noise (1500, 0.0f);
    {
        unsigned state = 99991u;
        for (auto& sample : noise)
        {
            state = state * 1103515245u + 12345u;
            sample = (float) ((state >> 16) % 1000u) / 1000.0f;
        }
    }
    const auto noiseGrid = stemdeck::BeatGridDecoder::decode (noise, fps);
    check (! noiseGrid || noiseGrid.confidence < cleanGrid.confidence,
           "noise never outscores a genuine grid");
    check (! noiseGrid || ! jitteredGrid || noiseGrid.confidence < jitteredGrid.confidence,
           "confidence orders clean > irregular > structureless");

    std::cout << "  confidence: clean=" << cleanGrid.confidence
              << " jittered=" << (jitteredGrid ? jitteredGrid.confidence : 0.0f)
              << " noise=" << (noiseGrid ? noiseGrid.confidence : 0.0f)
              << " (override floor " << kBeatConfidenceFloor << ")\n";

    // OPEN, DELIBERATELY NOT ASSERTED: structureless input still scores well ABOVE the
    // 0.35 override floor (~0.62 here), so the gate is too permissive in absolute terms
    // even though it now orders correctly. The cause is structural rather than arithmetic:
    // the tempo-state path is *constrained* to near-constant period, so it manufactures a
    // regular grid through anything — regularity measures the constraint as much as the
    // data. Making confidence reject noise outright needs an evidence term based on the
    // CONTRAST between chosen and unchosen frames, and choosing that threshold is a
    // calibration decision. Per CLAUDE.md §11, calibrating it against a synthetic noise
    // generator invented in this file is precisely the overfitting that has burned this
    // project before; it needs the licensed real-music corpus (§10 item 4). Asserting it
    // here would encode a fake number as a contract.
    if (noiseGrid && noiseGrid.confidence >= (float) kBeatConfidenceFloor)
        std::cout << "  note: structureless input scores " << noiseGrid.confidence
                  << " vs floor " << kBeatConfidenceFloor
                  << " — absolute calibration remains uncertified (needs real corpus)\n";

    if (failures == 0)
        std::cout << "Beat-grid decoder contract passed\n";
    return failures == 0 ? 0 : 1;
}
