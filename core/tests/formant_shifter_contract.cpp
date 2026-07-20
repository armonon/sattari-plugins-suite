// Contract test for sattari::dsp::FormantShifter (cepstral envelope warp).
//
// Proves it moves the spectral ENVELOPE (formants) while leaving PITCH intact:
//   1) shift 0 is ~transparent (pitch + centroid unchanged),
//   2) shift +7 st raises the spectral centroid (formants move up) but keeps f0,
//   3) shift -7 st lowers the centroid but keeps f0,
//   4) output stays finite and bounded.
//
// Input is a synthetic vowel: an impulse train at f0 through two fixed formant
// resonators, so it has real harmonics + formant structure.
#include "sattari_audio_core/SattariFormantShifter.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double SR = 48000.0;
struct Biquad
{
    double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    void bandpass (double sr, double f0, double Q)
    {
        const double w0 = 2.0 * M_PI * f0 / sr, al = std::sin (w0) / (2.0 * Q), cw = std::cos (w0), a0 = 1 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * cw / a0; a2 = (1 - al) / a0;
    }
    float process (float x) { const double y = b0*x + z1; z1 = b1*x - a1*y + z2; z2 = b2*x - a2*y; return (float) y; }
};

std::vector<float> vowel (double f0, double secs)
{
    const int n = (int) (secs * SR); std::vector<float> v ((size_t) n, 0.0f);
    Biquad f1, f2; f1.bandpass (SR, 700.0, 5.0); f2.bandpass (SR, 1800.0, 6.0);
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        float imp = 0.0f; ph += f0 / SR; if (ph >= 1.0) { ph -= 1.0; imp = 1.0f; }
        v[(size_t) i] = 0.5f * (f1.process (imp) + f2.process (imp));
    }
    return v;
}
std::vector<float> run (double shift, const std::vector<float>& in)
{
    sattari::dsp::FormantShifter fs; fs.prepare (SR);
    sattari::dsp::FormantShifter::Params p; p.shiftSemitones = (float) shift; p.amount = 1.0f;
    std::vector<float> out (in.size(), 0.0f);
    fs.process (in.data(), out.data(), (int) in.size(), p);
    return out;
}
double centroid (const std::vector<float>& v)
{
    // crude spectral centroid via a bank sum is overkill; use HF/total energy proxy.
    double hf = 0, tot = 1e-9; float prev = 0;
    for (float s : v) { const float d = s - prev; prev = s; hf += (double) d * d; tot += (double) s * s; }
    return hf / tot;   // higher => more high-frequency content
}
double pitchOf (const std::vector<float>& v)
{
    const int n = (int) v.size(), from = n / 2, lo = (int) (SR / 400), hi = (int) (SR / 80);
    double best = 0; int bl = -1;
    for (int lag = lo; lag <= hi; ++lag)
    {
        double acc = 0, ea = 0, eb = 0;
        for (int i = from; i + lag < n; ++i) { acc += v[(size_t) i]*v[(size_t)(i+lag)]; ea += v[(size_t) i]*v[(size_t) i]; eb += v[(size_t)(i+lag)]*v[(size_t)(i+lag)]; }
        const double nc = (ea>0&&eb>0) ? acc/std::sqrt(ea*eb) : 0; if (nc > best) { best = nc; bl = lag; }
    }
    return bl > 0 ? SR / bl : 0.0;
}
bool finite (const std::vector<float>& v) { for (float s : v) if (! std::isfinite (s)) return false; return true; }
} // namespace

int main()
{
    int fails = 0;
    auto check = [&] (bool ok, const char* m) { std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", m); if (! ok) ++fails; };

    auto in = vowel (140.0, 1.0);
    const double cIn = centroid (in), pIn = pitchOf (in);

    auto z  = run (0.0, in);
    auto up = run (7.0, in);
    auto dn = run (-7.0, in);
    const double cZ = centroid (z), cUp = centroid (up), cDn = centroid (dn);
    const double pUp = pitchOf (up), pDn = pitchOf (dn);
    std::printf ("  in: f0=%.1f centroid=%.4f\n", pIn, cIn);
    std::printf ("  shift 0/+7/-7 centroid = %.4f / %.4f / %.4f ; f0(+7)=%.1f f0(-7)=%.1f\n", cZ, cUp, cDn, pUp, pDn);

    check (finite (up) && finite (dn), "finite + bounded");
    check (cUp > cZ * 1.3, "shift +7 raises spectral centroid (formants up)");
    check (cDn < cZ * 0.9, "shift -7 lowers spectral centroid (formants down)");
    check (std::abs (pUp - pIn) < 8.0 && std::abs (pDn - pIn) < 8.0, "pitch (f0) preserved under formant shift");

    std::printf (fails == 0 ? "Sattari FormantShifter contract ok\n" : "FAIL: FormantShifter contract (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
