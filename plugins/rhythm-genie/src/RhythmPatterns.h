#pragma once

// =============================================================================
//  Sattari Rhythm Genie — pattern core (pure std, no JUCE)
// -----------------------------------------------------------------------------
//  A small bank of 16-step drum patterns plus the density logic that thins or
//  fills them. Pure-std so it unit-tests on its own like the rest of the shared
//  core. Each lane's 16 steps are packed into a uint16 (bit i = step i, LSB =
//  step 0). MIDI notes follow the General MIDI drum map.
// =============================================================================

#include <cstdint>
#include <cstddef>

namespace sattari::rhythm
{
enum Lane { Kick = 0, Snare, ClosedHat, OpenHat, kNumLanes };

inline int laneNote (int lane) noexcept
{
    static const int notes[kNumLanes] = { 36, 38, 42, 46 };  // GM: kick, snare, closed hat, open hat
    return (lane >= 0 && lane < kNumLanes) ? notes[lane] : 36;
}
inline const char* laneName (int lane) noexcept
{
    static const char* names[kNumLanes] = { "Kick", "Snare", "Closed Hat", "Open Hat" };
    return (lane >= 0 && lane < kNumLanes) ? names[lane] : "?";
}

static constexpr int kSteps = 16;

struct Pattern
{
    const char*   name;
    std::uint16_t lane[kNumLanes];   // step masks: bit i = a hit on 16th-step i
};

inline const Pattern* patterns (int& count) noexcept
{
    static const Pattern bank[] = {
        //                Kick     Snare    ClosedHat OpenHat
        { "Four on Floor", { 0x1111, 0x1010, 0x4444, 0x0000 } }, // house: kick every beat
        { "Boom Bap",      { 0x0441, 0x1010, 0x5555, 0x0000 } }, // hip-hop
        { "Trap",          { 0x0441, 0x0100, 0x5555, 0x0000 } }, // half-time snare
        { "Pop",           { 0x0101, 0x1010, 0x5555, 0x0000 } },
        { "Breakbeat",     { 0x0401, 0x1010, 0x5555, 0x2200 } },
        { "Half-Time",     { 0x0001, 0x0100, 0x5555, 0x0000 } },
    };
    count = (int) (sizeof (bank) / sizeof (bank[0]));
    return bank;
}

inline const Pattern& pattern (int styleIndex) noexcept
{
    int n = 0;
    const Pattern* b = patterns (n);
    if (styleIndex < 0) styleIndex = 0;
    if (styleIndex >= n) styleIndex = n - 1;
    return b[styleIndex];
}

inline bool baseHit (const Pattern& p, int lane, int step) noexcept
{
    if (lane < 0 || lane >= kNumLanes || step < 0 || step >= kSteps) return false;
    return ((p.lane[lane] >> step) & 1u) != 0u;
}

// Whether a lane fires on a step, after the Density control thins/fills it.
// Density only reshapes the hi-hat (subdivisions); kick/snare keep their groove
// but thin toward the downbeats at very low density.
inline bool stepActive (const Pattern& p, int lane, int step, float density) noexcept
{
    const bool base = baseHit (p, lane, step);
    if (lane == ClosedHat)
    {
        if (density >= 0.85f) return true;                       // full 16ths
        if (density >= 0.5f)  return base || (step % 2) == 0;    // 8ths + base
        if (density <  0.25f) return base && (step % 4) == 0;    // thin to downbeats
        return base;
    }
    if (density < 0.25f && lane != Snare)
        return base && (step % 4) == 0;
    return base;
}

} // namespace sattari::rhythm
