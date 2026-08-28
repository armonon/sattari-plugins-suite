// Contract test for sattari::dsp::VoxSynthEngine (VoxSynth continuous-pitch synth).
//
// Proves the synth-mode voice actually:
//   1) sounds at the DRIVEN pitch (220 Hz in -> ~220 Hz out),
//   2) the VCA follows the driven AMPLITUDE (0 -> silence, 0.8 -> audible), with no
//      ADSR of its own — the envelope IS the control,
//   3) the filter cutoff tracks the driven CENTROID (bright centroid -> more HF),
//   4) Quantize snaps a sharp pitch onto the scale (275 Hz, C major -> ~261.6 Hz),
//   5) output stays finite and bounded.
#include "sattari_audio_core/SattariVoxSynthEngine.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double SR = 48000.0;

// Drive a fixed target for `secs`, return the rendered mono buffer.
std::vector<float> drive (sattari::dsp::VoxSynthEngine& e, float f, float amp, float centroid, bool voiced, double secs)
{
    const int n = (int) (secs * SR);
    std::vector<float> out ((size_t) n, 0.0f);
    const int hop = 256;
    for (int pos = 0; pos < n; pos += hop)
    {
        e.setTarget (f, amp, centroid, voiced);
        const int m = std::min (hop, n - pos);
        e.render (out.data() + pos, m);
    }
    return out;
}

double rmsOf (const std::vector<float>& v, int from = 0)
{
    double e = 0; int c = 0;
    for (int i = from; i < (int) v.size(); ++i) { e += (double) v[(size_t) i] * v[(size_t) i]; ++c; }
    return c ? std::sqrt (e / c) : 0.0;
}

// Autocorrelation pitch over the tail (skip the attack/glide). Returns the SHORTEST
// lag whose correlation is within 90% of the max, so a saw's octave-down subharmonic
// peak doesn't win (that's a measurement artifact, not the synth's pitch).
double pitchOf (const std::vector<float>& v)
{
    const int n = (int) v.size(); const int from = n / 2;
    const int lo = (int) (SR / 500.0), hi = (int) (SR / 80.0);
    std::vector<double> nc ((size_t) (hi + 1), 0.0); double best = 0.0;
    for (int lag = lo; lag <= hi; ++lag)
    {
        double acc = 0, ea = 0, eb = 0;
        for (int i = from; i + lag < n; ++i) { acc += v[(size_t) i] * v[(size_t) (i + lag)]; ea += v[(size_t) i] * v[(size_t) i]; eb += v[(size_t) (i + lag)] * v[(size_t) (i + lag)]; }
        nc[(size_t) lag] = (ea > 0 && eb > 0) ? acc / std::sqrt (ea * eb) : 0.0;
        best = std::max (best, nc[(size_t) lag]);
    }
    for (int lag = lo; lag <= hi; ++lag)
        if (nc[(size_t) lag] >= 0.9 * best) return SR / lag;
    return 0.0;
}

// Fraction of energy above 2 kHz via a crude one-pole high-pass difference.
double hfRatio (const std::vector<float>& v)
{
    double hf = 0, tot = 1e-9; float prev = 0;
    for (float s : v) { const float d = s - prev; prev = s; hf += (double) d * d; tot += (double) s * s; }
    return hf / tot;
}
bool allFinite (const std::vector<float>& v) { for (float s : v) if (! std::isfinite (s)) return false; return true; }
} // namespace

int main()
{
    using namespace sattari::dsp;
    int fails = 0;
    auto check = [&] (bool ok, const char* m) { std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", m); if (! ok) ++fails; };

    // 1) Pitch.
    {
        VoxSynthEngine e; e.prepare (SR);
        VoxSynthEngine::Params p; p.osc1Wave = VoxSynthEngine::Saw; p.mix = 0.0f; p.filterAmount = 0.0f; p.filterBaseHz = 6000.0f; e.setParams (p);
        auto buf = drive (e, 220.0f, 0.8f, 1000.0f, true, 0.4);
        const double hz = pitchOf (buf);
        std::printf ("  test1 pitch=%.1f Hz (want 220)\n", hz);
        check (std::abs (hz - 220.0) < 8.0, "sounds at the driven pitch");
        check (allFinite (buf) && rmsOf (buf) < 1.0, "finite + bounded");
    }

    // 2) VCA follows amplitude.
    {
        VoxSynthEngine e; e.prepare (SR);
        auto silent = drive (e, 220.0f, 0.0f, 1000.0f, false, 0.1);
        auto loud   = drive (e, 220.0f, 0.8f, 1000.0f, true,  0.2);
        const double rs = rmsOf (silent), rl = rmsOf (loud, (int) (0.1 * SR));
        std::printf ("  test2 rms silent=%.4f loud=%.4f\n", rs, rl);
        check (rs < 0.005, "amplitude 0 -> silence");
        check (rl > 0.05, "amplitude 0.8 -> audible");
    }

    // 3) Centroid -> cutoff.
    {
        VoxSynthEngine e1; e1.prepare (SR); VoxSynthEngine e2; e2.prepare (SR);
        VoxSynthEngine::Params p; p.osc1Wave = VoxSynthEngine::Saw; p.mix = 0.0f; p.filterBaseHz = 200.0f; p.filterAmount = 1.0f; e1.setParams (p); e2.setParams (p);
        auto dark   = drive (e1, 220.0f, 0.8f, 400.0f,  true, 0.3);
        auto bright = drive (e2, 220.0f, 0.8f, 9000.0f, true, 0.3);
        const double hd = hfRatio (dark), hb = hfRatio (bright);
        std::printf ("  test3 hf(dark)=%.4f hf(bright)=%.4f\n", hd, hb);
        check (hb > hd * 1.5, "brighter centroid -> more HF energy");
    }

    // 4) Quantize snaps to scale.
    {
        VoxSynthEngine e; e.prepare (SR);
        bool cmaj[12] = { true,false,true,false,true,true,false,true,false,true,false,true };  // C major
        e.setScale (cmaj);
        VoxSynthEngine::Params p; p.osc1Wave = VoxSynthEngine::Saw; p.mix = 0.0f; p.filterAmount = 0.0f; p.filterBaseHz = 6000.0f; p.quantize = 1.0f; e.setParams (p);
        auto buf = drive (e, 275.0f, 0.8f, 1000.0f, true, 0.5);   // ~C#4-ish, sharp
        const double hz = pitchOf (buf);
        std::printf ("  test4 driven 275 Hz -> out %.1f Hz (want ~261.6 C4)\n", hz);
        check (std::abs (hz - 261.6) < 10.0, "quantize snaps sharp pitch onto the scale");
    }

    // --- MAQAM QUANTIZE -----------------------------------------------------
    //  The old quantiser searched for the nearest in-scale SEMITONE, so a neutral
    //  third was unreachable whatever scale you set — a voice near it was pulled
    //  to the major or minor third every time.
    //
    //  Measured DIFFERENTIALLY, on purpose. The neutral third (320.2 Hz) and the
    //  major third (329.6 Hz) are 9.4 Hz apart, which is smaller than this file's
    //  own pitch estimator error — test4 above lands 5 Hz off a known target. An
    //  absolute assertion here would be measuring the estimator, not the scale.
    //  Driving the SAME input through two scales cancels that error: whatever the
    //  estimator's bias, it applies to both, so the DIFFERENCE is trustworthy.
    {
        const int rastCents[]  = { 0, 200, 350, 500, 700, 900, 1050 };  // neutral 3rd
        const int majorCents[] = { 0, 200, 400, 500, 700, 900, 1100 };

        auto pitchWithScale = [&] (const int* cents) -> double
        {
            sattari::dsp::VoxSynthEngine engine;
            engine.prepare (SR);
            engine.setCentScale (sattari::dsp::fromCents (cents, 7, 0));
            sattari::dsp::VoxSynthEngine::Params p;
            p.quantize = 1.0f;      // fully snapped
            p.glideMs  = 0.0f;
            p.level    = 0.9f;
            engine.setParams (p);
            return pitchOf (drive (engine, 317.0f, 0.9f, 1200.0f, true, 0.5));
        };

        const double rastHz  = pitchWithScale (rastCents);
        const double majorHz = pitchWithScale (majorCents);
        const double centsApart = 1200.0 * std::log2 (majorHz / rastHz);

        std::printf ("  same 317 Hz input: rast %.1f Hz vs major %.1f Hz"
                     " = %.0f cents apart (want ~50)\n", rastHz, majorHz, centsApart);

        // A semitone quantiser gives BOTH scales the same answer, so any real
        // separation proves cents are being used. ~50 is the true figure.
        check (centsApart > 25.0,
               "rast and major snapped to the same pitch - quantiser is still semitone-based");
        check (centsApart < 80.0, "rast/major separation is not the expected ~50 cents");
        check (rastHz < majorHz, "the neutral third should sit BELOW the major third");
    }

    std::printf (fails == 0 ? "Sattari VoxSynthEngine contract ok\n" : "FAIL: VoxSynthEngine contract (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
