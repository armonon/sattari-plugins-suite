// Pure-C++ contract for the Vocal Master DSP helpers. No JUCE.
// g++ -std=c++17 -Icore/include plugins/vocal-master/tests/vocal_master_core_contract.cpp
#include "../src/VocalMasterDsp.h"
#include <cstdio>
#include <cmath>

using namespace sattari::vocal;

static int fails = 0;
static void chk (const char* w, bool ok) { if (! ok) { std::printf ("  FAIL %s\n", w); ++fails; } }
static bool near (float a, float b) { return std::fabs (a - b) < 1.0e-5f; }

int main()
{
    float l, r;

    // Full isolate collapses a hard-panned (anti-phase) "music" signal to centre.
    isolateCentre (1.0f, -1.0f, 1.0f, l, r);
    chk ("isolate=1 removes the sides", near (l, 0.0f) && near (r, 0.0f));

    // No isolate leaves the stereo image untouched.
    isolateCentre (1.0f, -1.0f, 0.0f, l, r);
    chk ("isolate=0 passes through", near (l, 1.0f) && near (r, -1.0f));

    // A centred (mono) signal is unaffected by isolate.
    isolateCentre (0.7f, 0.7f, 1.0f, l, r);
    chk ("centred signal kept", near (l, 0.7f) && near (r, 0.7f));

    // De-noise expander: full gain above threshold, floor at zero energy.
    chk ("expander above thr = 1", near (expanderGain (1.0f, 0.5f, 0.1f), 1.0f));
    chk ("expander at 0 = floor",  near (expanderGain (0.0f, 0.5f, 0.1f), 0.1f));
    chk ("expander half-way",      near (expanderGain (0.25f, 0.5f, 0.0f), 0.5f));

    // Tone tilt: bright boosts highs / cuts lows symmetrically.
    float lo, hi; tiltGainsDb (1.0f, lo, hi);
    chk ("tone +1 -> high +6 / low -6", near (hi, 6.0f) && near (lo, -6.0f));
    tiltGainsDb (0.0f, lo, hi);
    chk ("tone 0 -> flat", near (hi, 0.0f) && near (lo, 0.0f));

    if (fails == 0) std::printf ("vocal master core contract: OK\n");
    else            std::printf ("vocal master core contract: %d FAILURE(S)\n", fails);
    return fails == 0 ? 0 : 1;
}
