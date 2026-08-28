// Pure-C++ contract for the Maqam microtonal core. No JUCE.
// g++ -std=c++17 -Icore/include plugins/maqam/tests/maqam_core_contract.cpp
#include "../src/MaqamScales.h"
#include <cstdio>
#include <cmath>

using namespace sattari::maqam;

static int fails = 0;
static void chk (const char* w, bool ok) { if (! ok) { std::printf ("  FAIL %s\n", w); ++fails; } }
static bool near (double a, double b) { return std::fabs (a - b) < 1.0e-6; }

int main()
{
    int n = 0;
    maqamat (n);
    chk ("eight maqamat", n == 8);

    const auto& rast = maqam (0);
    chk ("Rast neutral 3rd = 350c", rast.cents[2] == 350);
    chk ("350c is neutral",  isNeutral (350));
    chk ("500c is not",     ! isNeutral (500));

    // Rast on C: a sung E (64) snaps to the half-flat 3rd (63.5); perfect 5th/root stay.
    chk ("Rast E -> 63.5 (half-flat)", near (snapMaqamMidi (64, 0, rast), 63.5));
    chk ("Rast G stays",               near (snapMaqamMidi (67, 0, rast), 67.0));
    chk ("Rast C stays",               near (snapMaqamMidi (60, 0, rast), 60.0));

    // Hijaz on D: a sung F (65) snaps up to F# — the augmented 2nd (D-Eb-F#).
    chk ("Hijaz F -> F# (aug 2nd)", near (snapMaqamMidi (65, 2, maqam (2)), 66.0));

    if (fails == 0) std::printf ("maqam core contract: OK\n");
    else            std::printf ("maqam core contract: %d FAILURE(S)\n", fails);
    return fails == 0 ? 0 : 1;
}
