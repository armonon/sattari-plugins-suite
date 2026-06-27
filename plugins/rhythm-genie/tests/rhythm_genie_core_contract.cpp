// Pure-C++ contract for the Rhythm Genie pattern core. No JUCE.
// g++ -std=c++17 -Icore/include plugins/rhythm-genie/tests/rhythm_genie_core_contract.cpp
#include "../src/RhythmPatterns.h"
#include <cstdio>

using namespace sattari::rhythm;

static int fails = 0;
static void chk (const char* what, bool ok) { if (! ok) { std::printf ("  FAIL %s\n", what); ++fails; } }

int main()
{
    int n = 0;
    patterns (n);
    chk ("six styles", n == 6);
    chk ("kick = GM 36", laneNote (Kick) == 36);
    chk ("open hat = GM 46", laneNote (OpenHat) == 46);

    const auto& four = pattern (0);   // Four on Floor
    chk ("FoF kick on 0",  baseHit (four, Kick, 0));
    chk ("FoF kick on 4",  baseHit (four, Kick, 4));
    chk ("FoF kick on 8",  baseHit (four, Kick, 8));
    chk ("FoF kick on 12", baseHit (four, Kick, 12));
    chk ("FoF kick off 1", ! baseHit (four, Kick, 1));
    chk ("FoF snare on 4", baseHit (four, Snare, 4));

    // Density reshapes the hi-hat: full 16ths at the top, thinned at the bottom.
    chk ("hat full density step 1", stepActive (four, ClosedHat, 1, 1.0f));
    chk ("hat full density step 3", stepActive (four, ClosedHat, 3, 1.0f));
    chk ("hat low density step 1 off", ! stepActive (four, ClosedHat, 1, 0.1f));

    if (fails == 0) std::printf ("rhythm genie core contract: OK\n");
    else            std::printf ("rhythm genie core contract: %d FAILURE(S)\n", fails);
    return fails == 0 ? 0 : 1;
}
