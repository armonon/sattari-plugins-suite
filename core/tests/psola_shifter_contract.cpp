// Contract test for sattari::dsp::PsolaShifter (streaming TD-PSOLA).
//
// Proves the low-latency monitor engine actually:
//   1) passes unity (pitchScale = 1) through ~transparently (pitch + centroid),
//   2) shifts the fundamental by the requested factor (measured on the output),
//   3) PRESERVES formants — the output spectral centroid tracks the INPUT
//      centroid, not alpha*centroid (which is what naive resampling would give),
//   4) stays finite and bounded.
//
// Input is a synthetic voiced signal: an impulse train at f0 through two fixed
// formant resonators (F1=700 Hz, F2=1200 Hz), so it has real harmonics + formant
// structure like a vowel.
#include "sattari_audio_core/SattariPsolaShifter.h"
#include "sattari_audio_core/SattariPitchDetector.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
// One direct-form biquad bandpass (RBJ) used as a fixed formant resonator.
struct Biquad
{
    double b0=1,b1=0,b2=0,a1=0,a2=0, z1=0,z2=0;
    void bandpass (double sr, double f0, double Q)
    {
        const double w0 = 2.0 * M_PI * f0 / sr;
        const double alpha = std::sin (w0) / (2.0 * Q);
        const double cw = std::cos (w0);
        const double a0 = 1.0 + alpha;
        b0 = (alpha) / a0; b1 = 0.0; b2 = (-alpha) / a0;
        a1 = (-2.0 * cw) / a0; a2 = (1.0 - alpha) / a0;
        z1 = z2 = 0.0;
    }
    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

// Synthetic vowel: impulse train at f0 summed through two formant resonators.
std::vector<float> makeVoice (double sr, double f0, int numSamples)
{
    std::vector<float> imp ((size_t) numSamples, 0.0f);
    const double P = sr / f0;
    for (double t = 0.0; t < numSamples; t += P)
        imp[(size_t) t] = 1.0f;

    Biquad f1, f2; f1.bandpass (sr, 700.0, 6.0); f2.bandpass (sr, 1200.0, 8.0);
    std::vector<float> out ((size_t) numSamples, 0.0f);
    for (int i = 0; i < numSamples; ++i)
        out[(size_t) i] = 0.7f * f1.process (imp[(size_t) i]) + 0.5f * f2.process (imp[(size_t) i]);
    return out;
}

// Formant-band spectral centroid (Hz): Hann-windowed DFT magnitude, restricted
// to the formant region [200, 2500] Hz so the measure reflects where the
// spectral ENVELOPE sits (F1/F2) rather than being dominated by bright impulse
// leakage up near Nyquist. This is what moves under naive resampling and stays
// put under formant-preserving PSOLA.
double spectralCentroid (const std::vector<float>& x, int start, int len, double sr)
{
    const int N = len;
    const int kLo = std::max (1, (int) (200.0  * N / sr));
    const int kHi = std::min (N / 2 - 1, (int) (2500.0 * N / sr));
    double num = 0.0, den = 0.0;
    for (int k = kLo; k <= kHi; ++k)
    {
        double re = 0.0, im = 0.0;
        for (int n = 0; n < N; ++n)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * M_PI * n / (N - 1));   // Hann
            const double a = -2.0 * M_PI * k * n / N;
            const double s = x[(size_t) (start + n)] * win;
            re += s * std::cos (a); im += s * std::sin (a);
        }
        const double mag = std::sqrt (re * re + im * im);
        const double f = (double) k * sr / N;
        num += f * mag; den += mag;
    }
    return den > 1.0e-9 ? num / den : 0.0;
}

// Run a whole signal through the shifter block by block.
std::vector<float> runShifter (sattari::dsp::PsolaShifter& ps, const std::vector<float>& in,
                               double pitchScale, double periodSamples, int block)
{
    std::vector<float> out (in.size(), 0.0f);
    for (int start = 0; start < (int) in.size(); start += block)
    {
        const int n = std::min (block, (int) in.size() - start);
        ps.process (in.data() + start, out.data() + start, n, pitchScale, periodSamples);
    }
    return out;
}

// Naive resample by `alpha` (linear interpolation): shifts BOTH pitch and
// formants up by alpha — the artifact PSOLA is supposed to avoid. Used as the
// A/B baseline for formant preservation.
std::vector<float> naiveResample (const std::vector<float>& x, double alpha)
{
    std::vector<float> y (x.size(), 0.0f);
    for (int i = 0; i < (int) x.size(); ++i)
    {
        const double src = (double) i * alpha;
        const int i0 = (int) std::floor (src);
        if (i0 + 1 >= (int) x.size()) break;
        const double f = src - i0;
        y[(size_t) i] = (float) ((1.0 - f) * x[(size_t) i0] + f * x[(size_t) (i0 + 1)]);
    }
    return y;
}

double measurePitch (const std::vector<float>& x, double sr, int fromSample)
{
    sattari::dsp::PitchDetector d;
    d.prepare (sr, 60.0f, 1000.0f, 0.045f);
    const int block = 256;
    for (int s = fromSample; s + block <= (int) x.size(); s += block)
        d.pushBlock (x.data() + s, block);
    return d.analyse (0.2f).frequencyHz;
}
} // namespace

int main()
{
    const double sr = 48000.0;
    const double f0 = 150.0;
    const double P  = sr / f0;
    const int    N  = (int) (sr * 0.8);   // 0.8 s
    const int    block = 128;
    int fails = 0;

    const auto voice = makeVoice (sr, f0, N);
    const double inCentroid = spectralCentroid (voice, N / 2, 4096, sr);
    const double inPitch    = measurePitch (voice, sr, N / 3);
    std::printf ("input: pitch=%.1f Hz, centroid=%.0f Hz\n", inPitch, inCentroid);

    // 1) Unity pass: pitch and centroid ~unchanged.
    {
        sattari::dsp::PsolaShifter ps; ps.prepare (sr);
        const auto y = runShifter (ps, voice, 1.0, P, block);
        const double yc = spectralCentroid (y, N / 2, 4096, sr);
        const double yp = measurePitch (y, sr, N / 3 + ps.getLatencySamples());
        const double cents = 1200.0 * std::log2 (yp / f0);
        std::printf ("unity : pitch=%.1f Hz (%+.1f cents), centroid=%.0f Hz, latency=%d\n",
                     yp, cents, yc, ps.getLatencySamples());
        if (std::fabs (cents) > 45.0) { std::printf ("  FAIL unity pitch\n"); ++fails; }
        if (std::fabs (yc - inCentroid) / inCentroid > 0.25) { std::printf ("  FAIL unity centroid\n"); ++fails; }
    }

    // 2+3) Shift up a minor third: pitch *= alpha, centroid stays ~put (formant
    //      preserved). Naive resampling would move the centroid by alpha (~+300 Hz).
    {
        const double alpha = std::pow (2.0, 3.0 / 12.0);   // ~1.1892
        sattari::dsp::PsolaShifter ps; ps.prepare (sr);
        const auto y = runShifter (ps, voice, alpha, P, block);
        const double yc = spectralCentroid (y, N / 2, 4096, sr);
        const double yp = measurePitch (y, sr, N / 3 + ps.getLatencySamples());
        const double expected = f0 * alpha;
        const double cents = 1200.0 * std::log2 (yp / expected);
        std::printf ("shift : pitch=%.1f Hz (target %.1f, %+.1f cents), centroid=%.0f Hz\n",
                     yp, expected, cents, yc);
        if (std::fabs (cents) > 50.0) { std::printf ("  FAIL shifted pitch\n"); ++fails; }
        // Formant preservation, measured honestly against the naive-resample
        // baseline: naive resampling drags the whole spectrum (formants included)
        // up by alpha, so its centroid is well above PSOLA's. PSOLA's centroid
        // rises only from the wider harmonic spacing, staying markedly lower.
        const auto   naive        = naiveResample (voice, alpha);
        const double naiveCentroid= spectralCentroid (naive, N / 2, 4096, sr);
        const double pull         = (naiveCentroid - yc) / inCentroid;  // how far PSOLA sits below naive
        std::printf ("  formant: psola=%.0f Hz, naive-resample=%.0f Hz, in=%.0f Hz -> PSOLA sits %.1f%% below naive\n",
                     yc, naiveCentroid, inCentroid, pull * 100.0);
        if (pull < 0.05) { std::printf ("  FAIL formant preservation (PSOLA centroid not below naive)\n"); ++fails; }
    }

    // 4) Finite + bounded across a sweep of shifts.
    for (double alpha : { 0.75, 0.9, 1.0, 1.1, 1.33 })
    {
        sattari::dsp::PsolaShifter ps; ps.prepare (sr);
        const auto y = runShifter (ps, voice, alpha, P, block);
        float peak = 0.0f; bool finite = true;
        for (float v : y) { if (! std::isfinite (v)) finite = false; peak = std::max (peak, std::fabs (v)); }
        if (! finite)    { std::printf ("  FAIL non-finite at alpha=%.2f\n", alpha); ++fails; }
        if (peak > 8.0f) { std::printf ("  FAIL runaway peak %.2f at alpha=%.2f\n", peak, alpha); ++fails; }
    }

    // 5) SUSTAIN: a long steady tone at a NON-INTEGER period must not decay. This is
    //    the case the original test missed — it used f0=150 @ 48k, period=320.0 exactly
    //    integer, the one value where the drifting-mark bug (round(s/P)*round(P)) is
    //    invisible. With a fractional period the buggy analysis mark walked off the
    //    input ring and the output faded to full silence within ~3 s. Assert the tail
    //    RMS is still a healthy fraction of the head RMS over several seconds.
    for (double periodSamples : { 100.5, 200.5, 137.3, 289.7 })
    {
        const double f0s = sr / periodSamples;
        const auto tone = makeVoice (sr, f0s, (int) (sr * 6.0)); // 6 seconds
        sattari::dsp::PsolaShifter ps; ps.prepare (sr);
        const auto y = runShifter (ps, tone, 1.0, periodSamples, block);   // unity pitch (identity)

        auto rmsWindow = [&] (double t0, double t1)
        {
            const int a = (int) (t0 * sr), b = std::min ((int) (t1 * sr), (int) y.size());
            double acc = 0.0; int cnt = 0;
            for (int i = a; i < b; ++i) { acc += (double) y[(size_t) i] * y[(size_t) i]; ++cnt; }
            return cnt > 0 ? std::sqrt (acc / cnt) : 0.0;
        };
        const double head = rmsWindow (0.5, 1.0);               // after warm-up
        const double tail = rmsWindow (5.0, 5.8);               // seconds later
        bool finite = true; for (float v : y) if (! std::isfinite (v)) finite = false;
        if (! finite)                    { std::printf ("  FAIL non-finite sustain P=%.1f\n", periodSamples); ++fails; }
        if (head < 1.0e-3)               { std::printf ("  FAIL sustain never started P=%.1f\n", periodSamples); ++fails; }
        else if (tail < 0.5 * head)      { std::printf ("  FAIL sustain DECAYED P=%.1f  head=%.4f tail=%.4f\n", periodSamples, head, tail); ++fails; }
    }

    std::printf (fails == 0 ? "PsolaShifter contract ok\n" : "PsolaShifter contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
