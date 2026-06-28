// Pure-C++ contract for the chromagram mapping + key chain. No JUCE.
// g++ -std=c++17 -Icore/include core/tests/chromagram_contract.cpp
#include <sattari_audio_core/SattariChromagram.h>
#include <sattari_audio_core/SattariKeyDetector.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace sattari::dsp;

static int fails = 0;
static void chk (const char* w, bool ok) { if (! ok) { std::printf ("  FAIL %s\n", w); ++fails; } }

int main()
{
    const int    fftSize = 4096;
    const double sr = 44100.0;
    const double binHz = sr / (double) fftSize;
    std::vector<float> mag ((size_t) (fftSize / 2 + 1), 0.0f);

    auto tone = [&] (double f) { const int b = (int) std::round (f / binHz);
                                 if (b > 0 && b < (int) mag.size()) mag[(size_t) b] += 1.0f; };
    tone (261.63); tone (329.63); tone (392.00);     // a C major triad: C4 E4 G4

    double chroma[12] = { 0 };
    accumulateChroma (mag.data(), (int) mag.size(), binHz, chroma);

    chk ("C present", chroma[0] > 0.0);
    chk ("E present", chroma[4] > 0.0);
    chk ("G present", chroma[7] > 0.0);
    chk ("C# silent", chroma[1] < 1.0e-9);

    double maxOther = 0.0;
    for (int i = 0; i < 12; ++i) if (i != 0 && i != 4 && i != 7) maxOther = std::max (maxOther, chroma[i]);
    chk ("chord tones dominate the rest", std::min ({ chroma[0], chroma[4], chroma[7] }) > maxOther);

    // The chroma -> key chain resolves the triad to C major.
    auto k = detectKey (chroma);
    chk ("triad -> valid key", k.valid);
    chk ("triad -> C root",    k.root == 0);

    // blendChroma rolls a frame into a normalised running estimate.
    double running[12] = { 0 };
    blendChroma (running, chroma, 1.0);
    double sum = 0.0; for (double v : running) sum += v;
    chk ("blend normalises to 1", std::fabs (sum - 1.0) < 1.0e-6);

    if (fails == 0) std::printf ("chromagram contract: OK\n");
    else            std::printf ("chromagram contract: %d FAILURE(S)\n", fails);
    return fails == 0 ? 0 : 1;
}
