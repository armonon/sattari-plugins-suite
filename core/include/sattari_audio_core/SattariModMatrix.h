#pragma once

// =============================================================================
//  SattariModMatrix — session-clocked parameter motion for the racks.
// -----------------------------------------------------------------------------
//  WHY THIS EXISTS. A rack of eight processors with no cross-module modulation
//  answers "why is this better than eight separate plugins?" with "presets and
//  one window". Modulation is the answer separate plugins structurally cannot
//  give: one clock moving parameters across every slot, in time with the song.
//
//  EVERY SHAPE IS DERIVED FROM THE TRANSPORT POSITION, not from a free-running
//  accumulator. Two consequences, both the point:
//
//    * it is phase-locked to the song, so a bar-long sweep starts at the bar and
//      two instances of the rack agree without being synchronised to each other;
//    * it is STATELESS across a transport jump — loop back four bars and the
//      motion is exactly where it was, because position is the only input.
//
//  An accumulator gets both of those wrong in ways that are hard to hear until
//  you loop a section and the filter is somewhere different each pass.
//
//  Random (sample-and-hold) is the one shape that needs a value per cycle rather
//  than per sample, and it is derived by HASHING THE CYCLE INDEX — so it is
//  still positionless, still identical on replay, and still different every
//  cycle. A live RNG would make the same bar sound different on every pass.
//
//  Pure std: no JUCE, no allocation, no locks. Safe on the audio thread.
// =============================================================================

#include <cmath>
#include <cstdint>

namespace sattari::dsp
{

enum class ModShape : int
{
    Sine = 0,
    Triangle,
    Saw,          // rises across the cycle
    Square,
    Random,       // sample-and-hold, one value per cycle
    Count
};

// Musical cycle lengths, in beats. Index order is the parameter's choice order.
inline double modCycleBeats (int index) noexcept
{
    static const double kBeats[] = {
        0.25,   // 1/16
        0.5,    // 1/8
        1.0,    // 1/4
        2.0,    // 1/2
        4.0,    // 1 bar
        8.0,    // 2 bars
        16.0,   // 4 bars
        32.0,   // 8 bars
    };
    constexpr int n = (int) (sizeof (kBeats) / sizeof (kBeats[0]));
    if (index < 0) index = 0;
    if (index >= n) index = n - 1;
    return kBeats[index];
}
inline constexpr int modCycleCount() noexcept { return 8; }

// Deterministic hash -> [0,1). Used for sample-and-hold so the same cycle always
// produces the same value, on every pass and in every instance.
inline float modHash01 (std::uint64_t x) noexcept
{
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (float) ((x >> 40) & 0xffffff) / 16777216.0f;
}

//  Bipolar -1..+1 value of `shape` at song position `ppq`.
//  `phaseOffset` in 0..1 lets several slots run the same shape out of phase.
inline float modValue (ModShape shape, double ppq, double cycleBeats,
                       float phaseOffset = 0.0f) noexcept
{
    if (cycleBeats <= 0.0) return 0.0f;

    const double cycles = ppq / cycleBeats + (double) phaseOffset;
    const double floorC = std::floor (cycles);
    const double phase  = cycles - floorC;              // 0..1 within the cycle

    switch (shape)
    {
        case ModShape::Sine:
            return (float) std::sin (phase * 2.0 * 3.14159265358979323846);

        case ModShape::Triangle:
            return (float) (phase < 0.5 ? (phase * 4.0 - 1.0) : (3.0 - phase * 4.0));

        case ModShape::Saw:
            return (float) (phase * 2.0 - 1.0);

        case ModShape::Square:
            return phase < 0.5 ? 1.0f : -1.0f;

        case ModShape::Random:
        {
            // One value per cycle, derived from WHICH cycle this is.
            const auto index = (std::uint64_t) (std::int64_t) floorC;
            return modHash01 (index * 0x9e3779b97f4a7c15ULL) * 2.0f - 1.0f;
        }

        case ModShape::Count:
        default:
            return 0.0f;
    }
}

} // namespace sattari::dsp
