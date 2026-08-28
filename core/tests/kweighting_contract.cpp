// Contract for the shared BS.1770 K-weighting + true-peak helpers.
//
// These back both Sattari Meter and Sattari Peak, and the coefficients come from a
// published standard — so the useful thing to lock is the STANDARD'S BEHAVIOUR
// (what the filters actually do to signals), not a transcription of the algebra,
// which would just restate the implementation.
#include <sattari_audio_core/SattariKWeighting.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
int failures = 0;
constexpr double kPi = 3.14159265358979323846;

void check (bool ok, const std::string& what)
{
    if (! ok) { std::printf ("  FAIL: %s\n", what.c_str()); ++failures; }
}

// Steady-state gain of a biquad chain at a frequency, by running a sine until settled.
double gainAt (double freqHz, double sr, bool shelf, bool hp)
{
    sattari::dsp::KBiquad s, h;
    if (shelf) sattari::dsp::designKWeightingShelf (s, sr);
    if (hp)    sattari::dsp::designKWeightingHighPass (h, sr);

    const int settle = (int) (sr * 0.5), measure = (int) (sr * 0.2);
    double peak = 0.0;
    for (int i = 0; i < settle + measure; ++i)
    {
        double x = std::sin (2.0 * kPi * freqHz * (double) i / sr);
        if (shelf) x = s.process (x);
        if (hp)    x = h.process (x);
        if (i >= settle) peak = std::max (peak, std::fabs (x));
    }
    return peak;
}

double dB (double g) { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }
} // namespace

int main()
{
    using namespace sattari::dsp;
    std::printf ("SattariAudioCore K-weighting contract\n");
    const double sr = 48000.0;

    // --- Stage 1: high shelf, ~+4 dB of HF lift -----------------------------------
    {
        const double lf = dB (gainAt (100.0,  sr, true, false));    // below the shelf
        const double hf = dB (gainAt (10000.0, sr, true, false));   // above the shelf
        check (std::fabs (lf) < 0.5, "shelf leaves low frequencies ~unity (got " + std::to_string (lf) + " dB)");
        check (hf > 3.0 && hf < 5.0, "shelf lifts highs by ~+4 dB (got " + std::to_string (hf) + " dB)");
        check (hf - lf > 3.0, "shelf tilts up overall");
    }

    // --- Stage 2: 38 Hz high pass --------------------------------------------------
    {
        const double sub = dB (gainAt (10.0,   sr, false, true));   // well below fc
        const double mid = dB (gainAt (1000.0, sr, false, true));   // well above fc
        check (sub < -12.0, "high pass rejects 10 Hz (got " + std::to_string (sub) + " dB)");
        check (std::fabs (mid) < 0.5, "high pass passes 1 kHz (got " + std::to_string (mid) + " dB)");
    }

    // --- Combined K-weighting: the 997 Hz reference is ~unity ----------------------
    {
        const double ref = dB (gainAt (997.0, sr, true, true));
        check (std::fabs (ref) < 1.0, "K-weighting is ~0 dB at the 997 Hz reference (got "
                                      + std::to_string (ref) + " dB)");
        const double rumble = dB (gainAt (20.0, sr, true, true));
        check (rumble < -6.0, "K-weighting attenuates 20 Hz rumble");
    }

    // --- Stability: no blow-up on a long noisy run ---------------------------------
    {
        KBiquad s, h;
        designKWeightingShelf (s, sr);
        designKWeightingHighPass (h, sr);
        double acc = 0.0;
        unsigned seed = 12345u;
        for (int i = 0; i < 200000; ++i)
        {
            seed = seed * 1103515245u + 12345u;
            const double noise = ((double) ((seed >> 16) & 0x7fff) / 16384.0) - 1.0;
            acc += h.process (s.process (noise));
        }
        check (std::isfinite (acc), "K-weighting chain stays finite over a long noisy run");
    }

    // --- Inter-sample peak ---------------------------------------------------------
    {
        check (std::fabs (interSamplePeak (1.0f, 1.0f) - 1.0f) < 1.0e-6f,
               "flat signal has no inter-sample overshoot");
        // A sign flip between samples passes through zero, so the true peak is just
        // the larger endpoint magnitude — never more.
        check (interSamplePeak (-1.0f, 1.0f) <= 1.0f + 1.0e-6f,
               "sign flip does not invent peak above the endpoints");
        check (interSamplePeak (0.0f, 0.8f) >= 0.8f - 1.0e-6f,
               "reports at least the current sample magnitude");
        check (interSamplePeak (0.0f, -0.9f) >= 0.9f - 1.0e-6f, "magnitude is sign-independent");
    }

    if (failures == 0) { std::printf ("K-weighting contract: PASS\n"); return 0; }
    std::printf ("K-weighting contract: %d FAILURE(S)\n", failures);
    return 1;
}
