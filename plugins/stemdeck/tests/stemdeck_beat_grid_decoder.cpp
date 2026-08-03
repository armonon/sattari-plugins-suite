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

    if (failures == 0)
        std::cout << "Beat-grid decoder contract passed\n";
    return failures == 0 ? 0 : 1;
}
