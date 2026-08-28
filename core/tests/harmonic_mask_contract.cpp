// Contract for the causal harmonic/percussive soft mask (SattariHarmonicMask.h).
//
// Locks the properties the Auto Pitch poly "Tonal Focus" routing depends on:
//   * a steady tonal partial reads as HARMONIC (w -> ~1),
//   * a broadband transient landing on top of tonal history reads as PERCUSSIVE (w -> ~0),
//   * the weight always stays in [0,1], and
//   * the mask is deterministic (same frames in => same weights out).
//
// This proves the ALGORITHM on magnitude spectra whose harmonic/percussive character is
// known by construction. It does not claim a perceptual mix result — that is measured
// separately by the Auto Pitch --polyfile A/B on real material.
#include <sattari_audio_core/SattariHarmonicMask.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
int failures = 0;
void check (bool ok, const std::string& what)
{
    if (! ok) { std::printf ("  FAIL: %s\n", what.c_str()); ++failures; }
}
using sattari::dsp::HarmonicMask;

constexpr int kBins = 513;      // e.g. a 1024-pt FFT
constexpr int kTone = 100;      // bin holding the steady partial
constexpr float kFloor = 0.01f; // quiet broadband floor

// A frame that is a single steady tone at kTone over a quiet flat floor.
std::vector<float> toneFrame()
{
    std::vector<float> f ((std::size_t) kBins, kFloor);
    f[(std::size_t) kTone] = 1.0f;
    return f;
}

// A frame that is a broadband burst: every bin loud (a click / hat spectrum).
std::vector<float> broadbandFrame()
{
    return std::vector<float> ((std::size_t) kBins, 1.0f);
}
} // namespace

int main()
{
    std::printf ("HarmonicMask contract\n");

    // --- 1. Steady tone reads as harmonic; range stays in [0,1] -----------------------
    {
        HarmonicMask m;
        m.prepare (kBins, 15, 15);
        std::vector<float> w ((std::size_t) kBins, 0.0f);
        const auto tone = toneFrame();
        for (int i = 0; i < 30; ++i) m.process (tone.data(), w.data(), 2.0f, 0.5f);

        bool inRange = true;
        for (int k = 0; k < kBins; ++k) inRange &= (w[(std::size_t) k] >= 0.0f && w[(std::size_t) k] <= 1.0f);
        check (inRange, "all weights within [0,1]");
        check (w[(std::size_t) kTone] > 0.85f, "steady partial reads as harmonic (w>0.85)");
        // A neighbour a couple of bins off the partial should be far less tonal than the peak.
        check (w[(std::size_t) (kTone + 40)] < w[(std::size_t) kTone], "off-partial floor less tonal than the partial");
    }

    // --- 2. Broadband transient over tonal history reads as percussive ----------------
    {
        HarmonicMask m;
        m.prepare (kBins, 15, 15);
        std::vector<float> w ((std::size_t) kBins, 0.0f);
        const auto tone = toneFrame();
        // Build a tonal history so the time-median at non-tone bins is the quiet floor.
        for (int i = 0; i < 20; ++i) m.process (tone.data(), w.data(), 2.0f, 0.0f /*no EMA, isolate this frame*/);
        // Now one broadband burst frame.
        const auto burst = broadbandFrame();
        m.process (burst.data(), w.data(), 2.0f, 0.0f);

        // A non-tonal bin in the burst: loud NOW (high P), quiet in the PAST (low H) -> percussive.
        const int nb = 300;
        check (w[(std::size_t) nb] < 0.2f, "broadband transient bin reads as percussive (w<0.2)");
        // The steady partial, even during the burst, should remain more tonal than the noise bin.
        check (w[(std::size_t) kTone] > w[(std::size_t) nb], "partial stays more tonal than transient during a burst");
    }

    // --- 3. Determinism ---------------------------------------------------------------
    {
        HarmonicMask a, b;
        a.prepare (kBins, 15, 15);
        b.prepare (kBins, 15, 15);
        std::vector<float> wa ((std::size_t) kBins), wb ((std::size_t) kBins);
        const auto tone = toneFrame();
        const auto burst = broadbandFrame();
        for (int i = 0; i < 12; ++i) { a.process (tone.data(), wa.data()); b.process (tone.data(), wb.data()); }
        a.process (burst.data(), wa.data());
        b.process (burst.data(), wb.data());
        bool identical = true;
        for (int k = 0; k < kBins; ++k) identical &= (wa[(std::size_t) k] == wb[(std::size_t) k]);
        check (identical, "same frames in => identical weights out");
    }

    std::printf (failures == 0 ? "HarmonicMask contract ok\n" : "HarmonicMask contract FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
