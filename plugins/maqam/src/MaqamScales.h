#pragma once

// =============================================================================
//  Sattari Maqam — microtonal maqam scale core (pure std, no JUCE)
// -----------------------------------------------------------------------------
//  Arabic / Persian / Turkish maqamat use *quarter-tone* (neutral) intervals
//  that don't exist in 12-TET, so the suite's semitone scale-snap can't tune to
//  them. Each maqam is stored as ascending CENTS offsets within an octave; the
//  half-flat / half-sharp degrees land on 50-cent steps. Pure-std so it unit
//  tests on its own like the rest of the shared core.
// =============================================================================

#include <cmath>
#include <cstddef>

namespace sattari::maqam
{
static constexpr int kMaxDegrees = 7;

struct Maqam
{
    const char* name;
    int numDegrees;
    int cents[kMaxDegrees];   // ascending within [0, 1200); 50-cent steps = neutral
};

inline const Maqam* maqamat (int& count) noexcept
{
    static const Maqam table[] = {
        //  name        N   degrees in cents (root .. before the octave)
        { "Rast",       7, { 0, 200, 350, 500, 700, 900, 1050 } }, // neutral 3rd + 7th
        { "Bayati",     7, { 0, 150, 300, 500, 700, 800, 1000 } }, // neutral 2nd
        { "Hijaz",      7, { 0, 100, 400, 500, 700, 800, 1000 } }, // augmented 2nd
        { "Saba",       7, { 0, 150, 300, 400, 700, 800, 1000 } }, // diminished 4th
        { "Kurd",       7, { 0, 100, 300, 500, 700, 800, 1000 } }, // phrygian-like
        { "Nahawand",   7, { 0, 200, 300, 500, 700, 800, 1100 } }, // minor-like
        { "Ajam",       7, { 0, 200, 400, 500, 700, 900, 1100 } }, // major-like
        { "Hijazkar",   7, { 0, 100, 400, 500, 700, 800, 1100 } }, // hijaz both jins
    };
    count = (int) (sizeof (table) / sizeof (table[0]));
    return table;
}

inline const Maqam& maqam (int idx) noexcept
{
    int n = 0;
    const Maqam* t = maqamat (n);
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    return t[idx];
}

// A degree that isn't a whole number of semitones is a neutral (quarter-tone).
inline bool isNeutral (int cents) noexcept { return (cents % 100) != 0; }

inline double midiToHz (double m)  noexcept { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
inline double hzToMidi (double hz) noexcept { return hz > 0.0 ? 69.0 + 12.0 * std::log2 (hz / 440.0) : -1.0; }

// Snap a continuous MIDI note to the nearest degree of `mq` rooted at pitch
// class `rootPc` (0-11), returning the corrected continuous MIDI note (which may
// be fractional, e.g. 63.5 for a half-flat E in Rast on C).
inline double snapMaqamMidi (double detectedMidi, int rootPc, const Maqam& mq) noexcept
{
    if (mq.numDegrees <= 0) return detectedMidi;

    const double relCents = (detectedMidi - (double) rootPc) * 100.0;
    const double oct      = std::floor (relCents / 1200.0);
    const double within   = relCents - oct * 1200.0;          // [0, 1200)

    double bestCents = (double) mq.cents[0];
    double bestDist  = 1.0e18;
    for (int i = 0; i < mq.numDegrees; ++i)
        for (int o = -1; o <= 1; ++o)                          // allow wrap to the octave
        {
            const double cand = (double) mq.cents[i] + 1200.0 * o;
            const double d    = std::fabs (within - cand);
            if (d < bestDist) { bestDist = d; bestCents = cand; }
        }

    const double targetCents = oct * 1200.0 + bestCents;
    return (double) rootPc + targetCents / 100.0;
}

} // namespace sattari::maqam
