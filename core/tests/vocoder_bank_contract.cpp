// Contract test for sattari::dsp::VocoderBank (channel vocoder).
//
// Proves the CARRIER takes on the MODULATOR's spectral shape:
//   1) amount 0 = carrier passthrough,
//   2) a LOW modulator tone -> output energy sits low; a HIGH modulator tone -> high
//      (i.e. the vocoded output's brightness tracks the MODULATOR, not the carrier),
//   3) finite + bounded.
//
// Carrier is a bright saw (energy across the spectrum); the modulator picks which
// bands survive.
#include "sattari_audio_core/SattariVocoderBank.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double SR = 48000.0, TWO_PI = 6.283185307179586;
std::vector<float> saw (double f0, int n)
{
    std::vector<float> v ((size_t) n, 0.0f); double ph = 0;
    for (int i = 0; i < n; ++i) { double s = 0; for (int h = 1; h <= 40; ++h) s += std::sin (h * ph) / h; v[(size_t) i] = 0.3f * (float) s; ph += TWO_PI * f0 / SR; if (ph > TWO_PI) ph -= TWO_PI; }
    return v;
}
std::vector<float> sine (double f0, int n)
{
    std::vector<float> v ((size_t) n, 0.0f); double ph = 0;
    for (int i = 0; i < n; ++i) { v[(size_t) i] = 0.7f * (float) std::sin (ph); ph += TWO_PI * f0 / SR; if (ph > TWO_PI) ph -= TWO_PI; }
    return v;
}
double hfRatio (const std::vector<float>& v)
{
    double hf = 0, tot = 1e-9; float prev = 0;
    for (float s : v) { const float d = s - prev; prev = s; hf += (double) d * d; tot += (double) s * s; }
    return hf / tot;
}
bool finite (const std::vector<float>& v) { for (float s : v) if (! std::isfinite (s)) return false; return true; }
} // namespace

int main()
{
    using namespace sattari::dsp;
    int fails = 0;
    auto check = [&] (bool ok, const char* m) { std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", m); if (! ok) ++fails; };

    const int n = (int) (0.4 * SR);
    auto carrier = saw (110.0, n);
    auto modLow  = sine (300.0, n);
    auto modHigh = sine (5000.0, n);

    // 1) passthrough at amount 0.
    { VocoderBank vb; vb.prepare (SR); std::vector<float> o ((size_t) n);
      vb.process (carrier.data(), modLow.data(), o.data(), n, 0.0f);
      double d = 0; for (int i = 0; i < n; ++i) d += std::abs (o[(size_t) i] - carrier[(size_t) i]);
      check (d / n < 1e-4, "amount 0 -> carrier passthrough"); }

    // 2) brightness tracks the modulator.
    std::vector<float> oLow ((size_t) n), oHigh ((size_t) n);
    { VocoderBank vb; vb.prepare (SR); vb.process (carrier.data(), modLow.data(),  oLow.data(),  n, 1.0f); }
    { VocoderBank vb; vb.prepare (SR); vb.process (carrier.data(), modHigh.data(), oHigh.data(), n, 1.0f); }
    const double hL = hfRatio (oLow), hH = hfRatio (oHigh);
    std::printf ("  hf(lowMod)=%.4f hf(highMod)=%.4f\n", hL, hH);
    check (hH > hL * 2.0, "output brightness follows the modulator, not the carrier");
    check (finite (oLow) && finite (oHigh), "finite + bounded");

    std::printf (fails == 0 ? "Sattari VocoderBank contract ok\n" : "FAIL: VocoderBank contract (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
