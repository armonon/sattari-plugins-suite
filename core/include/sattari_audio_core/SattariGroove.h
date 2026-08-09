#pragma once

#include <sattari_audio_core/SattariBeatGrid.h>
#include <cstdint>

// =============================================================================
//  SattariGroove — the generative "machine" for the beat grid.
// -----------------------------------------------------------------------------
//  Fills and evolves a BeatGrid so the user can "let the machine fly": Euclidean
//  rhythms distributed per style, a Density macro that thins or fills, deterministic
//  mutation for evolving variations, and one-shot fills. Deterministic from a seed so
//  it is contract-tested and reproducible.
//
//  Lane convention matches the kit defaults: 0 kick, 1 snare, 2 closed hat, 3 open hat
//  (higher lanes left untouched so hand-placed / stacked sounds survive a regenerate).
//  Pure-std, no allocation.
// =============================================================================

namespace sattari::dsp
{

enum class GrooveStyle { House = 0, BoomBap, Trap, Breakbeat, FourFloor, HalfTime, kCount };

// Euclidean rhythm: distribute `pulses` as evenly as possible across `steps`, optionally
// rotated. Bit i set = a hit on step i.
inline std::uint64_t euclid (int pulses, int steps, int rotation = 0) noexcept
{
    if (steps <= 0) return 0;
    if (pulses <= 0) return 0;
    if (pulses > steps) pulses = steps;
    std::uint64_t mask = 0;
    int prevBucket = -1;
    for (int i = 0; i < steps; ++i)
    {
        const int bucket = (i * pulses) / steps;
        if (bucket != prevBucket)
        {
            int pos = (i + rotation) % steps;
            if (pos < 0) pos += steps;
            mask |= (std::uint64_t) 1 << pos;
            prevBucket = bucket;
        }
    }
    return mask;
}

inline int popcount64 (std::uint64_t x) noexcept
{
    int c = 0; while (x) { x &= (x - 1); ++c; } return c;
}

// A tiny deterministic RNG so generation is reproducible from a seed.
struct GrooveRng
{
    std::uint32_t s;
    explicit GrooveRng (std::uint32_t seed) : s (seed ? seed : 0x9e3779b9u) {}
    std::uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() noexcept { return (float) (next() & 0xffffff) / 16777216.0f; }   // [0,1)
};

// Fill the drum lanes of `g` from a style + density (0..1). Only lanes 0..3 are touched.
inline void generate (BeatGrid& g, GrooveStyle style, float density, std::uint32_t seed) noexcept
{
    const int steps = g.steps();
    const int bar = 16;                      // 16th-note grid reference within a bar
    const float d = density < 0 ? 0 : (density > 1 ? 1 : density);
    GrooveRng rng (seed);

    for (int lane = 0; lane < 4 && lane < g.lanes(); ++lane) g.clearLane (lane);

    // Per-style pulse counts (scaled by density) over one bar, then tiled across the loop.
    int kickP = 0, hatP = 0, openEvery = 0;
    switch (style)
    {
        case GrooveStyle::FourFloor: kickP = 4;                 hatP = 8;  openEvery = 4; break;
        case GrooveStyle::House:     kickP = 4;                 hatP = 8;  openEvery = 2; break;
        case GrooveStyle::BoomBap:   kickP = 2 + (int) (2 * d); hatP = 8;  openEvery = 0; break;
        case GrooveStyle::Trap:      kickP = 2 + (int) (3 * d); hatP = 12; openEvery = 0; break;
        case GrooveStyle::Breakbeat: kickP = 3 + (int) (2 * d); hatP = 8;  openEvery = 8; break;
        case GrooveStyle::HalfTime:  kickP = 1 + (int) (2 * d); hatP = 8;  openEvery = 0; break;
        case GrooveStyle::kCount:                                              break;
        default: break;
    }
    hatP = (int) (hatP * (0.4f + 0.6f * d));            // density thins/fills the hats
    if (kickP < 1) kickP = 1;

    const std::uint64_t kickMask = euclid (kickP, bar);
    const std::uint64_t hatMask  = euclid (hatP,  bar);
    const bool halfTime = (style == GrooveStyle::Trap || style == GrooveStyle::HalfTime);

    for (int s = 0; s < steps; ++s)
    {
        const int b = s % bar;
        // Kick
        if (kickMask & ((std::uint64_t) 1 << b)) g.set (0, s, true, 0.9f, 1.0f);
        // Snare on the backbeat (beats 2 & 4 = steps 4 & 12; half-time = step 8 only)
        if (g.lanes() > 1)
        {
            const bool snare = halfTime ? (b == 8) : (b == 4 || b == 12);
            if (snare) g.set (1, s, true, 0.85f, 1.0f);
        }
        // Closed hat (probability dips as density drops, for a looser feel)
        if (g.lanes() > 2 && (hatMask & ((std::uint64_t) 1 << b)))
            g.set (2, s, true, 0.6f + 0.2f * rng.uni(), 0.7f + 0.3f * d);
        // Open hat accents
        if (g.lanes() > 3 && openEvery > 0 && (b % openEvery) == (openEvery / 2))
            g.set (3, s, true, 0.7f, 0.8f);
    }
}

// Evolve the grid: randomly toggle a fraction (~amount) of drum-lane cells, so the groove
// keeps changing. Deterministic per seed. Only lanes 0..3.
inline void mutate (BeatGrid& g, float amount, std::uint32_t seed) noexcept
{
    const float a = amount < 0 ? 0 : (amount > 1 ? 1 : amount);
    GrooveRng rng (seed);
    const int laneMax = g.lanes() < 4 ? g.lanes() : 4;
    for (int lane = 0; lane < laneMax; ++lane)
        for (int s = 0; s < g.steps(); ++s)
            if (rng.uni() < a * 0.25f)   // cap churn so a full mutate still resembles the groove
                g.toggle (lane, s);
}

// One-shot fill: add a burst of hits into a lane over the last `region` steps (e.g. the
// end of a bar) for a drum fill. Deterministic per seed.
inline void addFill (BeatGrid& g, int lane, int regionSteps, float intensity, std::uint32_t seed) noexcept
{
    const int steps = g.steps();
    if (regionSteps <= 0 || regionSteps > steps) regionSteps = steps;
    const float in = intensity < 0 ? 0 : (intensity > 1 ? 1 : intensity);
    GrooveRng rng (seed);
    for (int s = steps - regionSteps; s < steps; ++s)
        if (rng.uni() < 0.3f + 0.6f * in)
            g.set (lane, s, true, 0.6f + 0.4f * rng.uni(), 1.0f);
}

} // namespace sattari::dsp
