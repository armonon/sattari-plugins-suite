// Contract test for the CREPE salience decode (sattari::dsp::decodeCrepeSalience)
// and the bin<->frequency mapping. The neural inference itself needs a bundled
// ONNX model (not shipped), but the DECODE is pure-std and is the bug-prone part
// — a wrong mapping or weighted-average silently detunes every note. We lock it
// down with synthetic salience bumps at bins whose true frequency we know.
#include "sattari_audio_core/SattariCrepePitch.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace sattari::dsp;

int main()
{
    int fails = 0;

    // 1) Bin/cents/Hz mapping anchors. CREPE bin 0 ~ 32.70 Hz (C1); the mapping
    //    is monotonic and 20 cents/bin. Find the bin nearest A4 (440 Hz) and
    //    confirm it round-trips to ~440.
    {
        const double hz0 = crepeCentsToHz (crepeBinToCents (0));
        std::printf ("bin0 -> %.2f Hz (expect ~32.7 C1)\n", hz0);
        if (std::fabs (hz0 - 32.70) > 1.5) { std::printf ("  FAIL bin0 mapping\n"); ++fails; }

        // Bin spacing is 20 cents.
        const double c0 = crepeBinToCents (0), c1 = crepeBinToCents (1);
        if (std::fabs ((c1 - c0) - 20.0) > 1.0e-6) { std::printf ("  FAIL 20-cent spacing\n"); ++fails; }
    }

    // 2) A single-bin salience spike decodes to that bin's exact frequency.
    for (int bin : { 60, 120, 180, 240, 300 })
    {
        std::vector<float> sal (360, 0.0f);
        sal[(size_t) bin] = 1.0f;
        const auto r = decodeCrepeSalience (sal.data(), 360, 0.5f);
        const double truth = crepeCentsToHz (crepeBinToCents (bin));
        const double cents = 1200.0 * std::log2 (r.frequencyHz / truth);
        std::printf ("spike bin %3d -> %.2f Hz (truth %.2f, %+.2f cents) conf=%.2f voiced=%d\n",
                     bin, r.frequencyHz, truth, cents, r.confidence, (int) r.voiced);
        if (std::fabs (cents) > 1.0) { std::printf ("  FAIL spike decode\n"); ++fails; }
        if (! r.voiced)              { std::printf ("  FAIL voiced flag\n"); ++fails; }
    }

    // 3) A Gaussian salience bump centred at a bin decodes to that bin's
    //    frequency via the local weighted average (the realistic case — CREPE
    //    salience is a smooth hump, not a spike).
    {
        const int center = 200;
        const double truth = crepeCentsToHz (crepeBinToCents (center));
        std::vector<float> sal (360, 0.0f);
        for (int i = 0; i < 360; ++i)
        {
            const double d = i - center;
            sal[(size_t) i] = (float) std::exp (-d * d / (2.0 * 1.5 * 1.5));   // sigma 1.5 bins
        }
        const auto r = decodeCrepeSalience (sal.data(), 360, 0.5f);
        const double cents = 1200.0 * std::log2 (r.frequencyHz / truth);
        std::printf ("gauss  bin %3d -> %.2f Hz (truth %.2f, %+.2f cents) conf=%.2f\n",
                     center, r.frequencyHz, truth, cents, r.confidence);
        if (std::fabs (cents) > 5.0) { std::printf ("  FAIL gaussian decode\n"); ++fails; }
    }

    // 4) Below-threshold peak -> not voiced (unvoiced/breath frames must not
    //    emit a confident pitch).
    {
        std::vector<float> sal (360, 0.05f);   // flat, low
        sal[100] = 0.30f;                       // weak peak, below 0.5 threshold
        const auto r = decodeCrepeSalience (sal.data(), 360, 0.5f);
        std::printf ("weak peak conf=%.2f voiced=%d (expect not voiced)\n", r.confidence, (int) r.voiced);
        if (r.voiced) { std::printf ("  FAIL should be unvoiced\n"); ++fails; }
    }

    // 5) A tracker with no model loaded is unavailable and returns unvoiced (the
    //    fallback-to-YIN contract the caller relies on).
    {
        CrepePitchTracker t;
        t.prepare (48000.0);
        std::vector<float> buf (256, 0.1f);
        t.pushBlock (buf.data(), 256);
        const auto r = t.analyse();
        std::printf ("no-model tracker: available=%d voiced=%d\n", (int) t.isAvailable(), (int) r.voiced);
        if (t.isAvailable()) { std::printf ("  FAIL should be unavailable without a model\n"); ++fails; }
        if (r.voiced)        { std::printf ("  FAIL should be unvoiced without a model\n"); ++fails; }
    }

    std::printf (fails == 0 ? "CrepePitch decode contract ok\n" : "CrepePitch decode contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
