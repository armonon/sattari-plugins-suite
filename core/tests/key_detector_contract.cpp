// Pure-C++ contract for the shared key detector. No JUCE.
// g++ -std=c++17 -Icore/include core/tests/key_detector_contract.cpp
#include <sattari_audio_core/SattariKeyDetector.h>
#include <cstdio>

using namespace sattari::dsp;

static int fails = 0;
static void chk (const char* w, bool ok) { if (! ok) { std::printf ("  FAIL %s\n", w); ++fails; } }

int main()
{
    // A chroma shaped like the C-major profile must resolve to C major.
    double cmaj[12];
    for (int i = 0; i < 12; ++i) cmaj[i] = detail::kKrumhanslMajor[i];
    auto kc = detectKey (cmaj);
    chk ("C-major chroma -> valid", kc.valid);
    chk ("C-major chroma -> root C", kc.root == 0);
    chk ("C-major chroma -> major", ! kc.minor);

    // A chroma shaped like the A-minor profile (minor profile rooted at A=9)
    // must resolve to A minor — the relative-key disambiguation Krumhansl gives.
    double amin[12];
    for (int i = 0; i < 12; ++i) amin[(i + 9) % 12] = detail::kKrumhanslMinor[i];
    auto ka = detectKey (amin);
    chk ("A-minor chroma -> root A", ka.root == 9);
    chk ("A-minor chroma -> minor", ka.minor);

    // Silence is invalid (no key).
    double zero[12] = { 0 };
    chk ("silent chroma -> invalid", ! detectKey (zero).valid);

    // Fixed-mode helper still finds the right root (the existing AutoKey path).
    chk ("fixed-mode major root C", detectKeyRootForMode (cmaj, false) == 0);
    chk ("fixed-mode minor root A", detectKeyRootForMode (amin, true) == 9);

    if (fails == 0) std::printf ("key detector contract: OK\n");
    else            std::printf ("key detector contract: %d FAILURE(S)\n", fails);
    return fails == 0 ? 0 : 1;
}
