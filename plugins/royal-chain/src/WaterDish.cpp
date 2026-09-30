#include "WaterDish.h"
#include "CymaticPlate.h"   // rc::CymaticPlate::besselJ / besselJp (validated to 1e-5)

#include <algorithm>
#include <cmath>

namespace rc
{
namespace
{
constexpr double kPiD = 3.14159265358979323846;
constexpr float kTwoPi = 6.28318530718f;

inline float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf (float a, float b, float t) noexcept { return a + (b - a) * t; }
inline float kOf (float dt, float tau) noexcept { return 1.0f - std::exp (-dt / tau); }

// --- generator: besselPrimeZeros (scan from 0.3 in 0.05 steps, 60-step bisection) ---
std::vector<double> besselPrimeZeros (int n, double alphaMax)
{
    std::vector<double> zeros;
    const double step = 0.05;
    double prevX = 0.3, prev = CymaticPlate::besselJp (n, prevX);
    for (double x = prevX + step; x <= alphaMax; x += step)
    {
        const double cur = CymaticPlate::besselJp (n, x);
        if (prev * cur < 0.0)
        {
            double a = prevX, b = x, fa = prev;
            for (int i = 0; i < 60; ++i)
            {
                const double m = 0.5 * (a + b), fm = CymaticPlate::besselJp (n, m);
                if (fa * fm <= 0.0) b = m; else { a = m; fa = fm; }
            }
            zeros.push_back (0.5 * (a + b));
        }
        prevX = x; prev = cur;
    }
    return zeros;
}

// --- generator: freqToWavelength + wavelengthToRGB (Bruton) + boostRGB ---
double freqToWavelength (double fAudio)
{
    double f = fAudio > 0.0 ? fAudio : 440.0;
    const double lo = 400e12, hi = 790e12;
    while (f < lo) f *= 2.0;
    while (f > hi) f /= 2.0;
    return (299792458.0 / f) * 1e9;
}

void wavelengthToRGB (double wl, float out[3])
{
    double r = 0, g = 0, b = 0;
    if (wl >= 380 && wl < 440)      { r = -(wl - 440) / 60; b = 1; }
    else if (wl < 490)              { g = (wl - 440) / 50; b = 1; }
    else if (wl < 510)              { g = 1; b = -(wl - 510) / 20; }
    else if (wl < 580)              { r = (wl - 510) / 70; g = 1; }
    else if (wl < 645)              { r = 1; g = -(wl - 645) / 65; }
    else if (wl <= 781)             { r = 1; }
    double f = 1;
    if (wl >= 380 && wl < 420)      f = 0.3 + 0.7 * (wl - 380) / 40;
    else if (wl > 700 && wl <= 781) f = 0.3 + 0.7 * (780 - wl) / 80;
    else if (wl < 380 || wl > 781)  f = 0;
    auto adj = [f] (double c) { return c <= 0 ? 0.0f : (float) std::round (255.0 * std::pow (c * f, 0.8)); };
    out[0] = adj (r); out[1] = adj (g); out[2] = adj (b);
}

void spectralColour (double hz, std::uint8_t out[3])
{
    float rgb[3];
    wavelengthToRGB (freqToWavelength (hz), rgb);
    const float m = std::max ({ rgb[0], rgb[1], rgb[2], 1.0f });
    const float k = 235.0f / m;   // keep the hue vivid
    for (int i = 0; i < 3; ++i)
        out[i] = (std::uint8_t) std::round (clampf (rgb[i] * k, 0.0f, 255.0f));
}

// RMS of the newest n samples, in dBFS.
float tailDb (const float* x, int total, int n) noexcept
{
    double acc = 0.0;
    for (int i = total - n; i < total; ++i) acc += (double) x[i] * x[i];
    const double rms = std::sqrt (acc / n);
    return rms > 1.0e-7 ? (float) (20.0 * std::log10 (rms)) : -140.0f;
}

// Normalised resonance response of a driven oscillator: 1 on resonance.
inline float resonance (float ratio, float Q) noexcept
{
    const float d1 = 1.0f - ratio * ratio, d2 = ratio / Q;
    return 1.0f / (Q * std::sqrt (d1 * d1 + d2 * d2));
}
} // namespace

//==============================================================================
void WaterDish::prepare()
{
    // --- buildCircularDish (GS, 64 modes, alphaMax 26, nMax 14, Neumann) ---
    struct Spec { int n, s; double alpha; };
    std::vector<Spec> specs;
    for (int n = 0; n <= kMaxN; ++n)
    {
        const auto roots = besselPrimeZeros (n, 26.0);
        for (size_t i = 0; i < roots.size(); ++i)
            specs.push_back ({ n, (int) i + 1, roots[i] });
    }
    std::sort (specs.begin(), specs.end(), [] (const Spec& p, const Spec& q) { return p.alpha < q.alpha; });
    if ((int) specs.size() > kMaxModes)
        specs.resize ((size_t) kMaxModes);

    const int N = kGS * kGS;
    const float edge = 1.0f + 1.5f / (0.5f * (float) kGS);    // bake 1.5 px past the rim for AA
    inside.assign ((size_t) N, 0);
    cellLut.assign ((size_t) N, 0);
    cosT.assign ((size_t) (kMaxN + 1) * N, 0.0f);
    sinT.assign ((size_t) (kMaxN + 1) * N, 0.0f);
    for (int j = 0; j < kGS; ++j)
    {
        const float cy = ((j + 0.5f) / kGS) * 2.0f - 1.0f;
        for (int i = 0; i < kGS; ++i)
        {
            const float cx = ((i + 0.5f) / kGS) * 2.0f - 1.0f;
            const int idx = j * kGS + i;
            const float r = std::hypot (cx, cy);
            if (r > edge) continue;
            inside[(size_t) idx] = 1;
            cellLut[(size_t) idx] = (std::uint16_t) std::lround (std::min (r / edge, 1.0f) * kLut);
            const float th = std::atan2 (cy, cx);
            for (int n = 0; n <= kMaxN; ++n)
            {
                cosT[(size_t) n * N + idx] = std::cos ((float) n * th);
                sinT[(size_t) n * N + idx] = std::sin ((float) n * th);
            }
        }
    }

    // Radial LUTs over [0, edge], normalised so each full mode J_n cos(n theta)
    // peaks at 1 inside the dish (the generator normalises every shape by its peak).
    modes.assign (specs.size(), Mode {});
    radialLut.assign (specs.size() * (size_t) (kLut + 1), 0.0f);
    for (size_t k = 0; k < specs.size(); ++k)
    {
        const auto& sp = specs[k];
        float* lut = radialLut.data() + k * (size_t) (kLut + 1);
        float peak = 0.0f;
        for (int i = 0; i <= kLut; ++i)
        {
            const double r = edge * (i / (double) kLut);
            lut[i] = (float) CymaticPlate::besselJ (sp.n, sp.alpha * r);
            if (r <= 1.0) peak = std::max (peak, std::abs (lut[i]));
        }
        if (peak > 0.0f)
            for (int i = 0; i <= kLut; ++i) lut[i] /= peak;
        modes[k].n = sp.n; modes[k].s = sp.s; modes[k].alpha = (float) sp.alpha;
    }

    // --- retuneDish (water, radius 23 mm, depth 9.4 mm) ---
    {
        const double R = 0.023, H = 0.0094, rho = 998.0, gamma = 0.0728, mu = 0.00100;
        const double nu = mu / rho;
        for (auto& m : modes)
        {
            const double k = m.alpha / R;
            const double w2 = (9.81 * k + (gamma / rho) * k * k * k) * std::tanh (k * H);
            m.hz = (float) (std::sqrt (std::max (w2, 0.0)) / kPiD);
            const double naturalOmega = kPiD * m.hz;
            const double decayRate = 2.0 * nu * k * k;
            m.q = (float) (naturalOmega / std::max (2.0 * decayRate, 1e-9));
        }
        f1 = modes.empty() ? 20.0f : std::max (1.0f, modes[0].hz);
        for (auto& m : modes) m.norm = m.hz / f1;
        baseHz = f1;
    }

    // Per-pixel geometry of the output image (dish units; angle 0 at the top, clockwise).
    radius.assign ((size_t) kImage * kImage, 0.0f);
    angle01.assign ((size_t) kImage * kImage, 0.0f);
    const float half = 0.5f * (float) kGS;
    for (int y = 0; y < kImage; ++y)
        for (int x = 0; x < kImage; ++x)
        {
            const float dx = (x + 0.5f - kImage * 0.5f) / half, dy = (y + 0.5f - kImage * 0.5f) / half;
            radius[(size_t) (y * kImage + x)] = std::hypot (dx, dy);
            float a = std::atan2 (dx, -dy) / kTwoPi;   // 0 = top, clockwise
            if (a < 0.0f) a += 1.0f;
            angle01[(size_t) (y * kImage + x)] = a;
        }

    // --- analysis ---
    fft = std::make_unique<juce::dsp::FFT> (kFFTOrder);
    window.assign ((size_t) kFFT, 0.0f);
    for (int i = 0; i < kFFT; ++i)   // Blackman, as Web Audio's AnalyserNode
    {
        const double a = 2.0 * kPiD * i / kFFT;
        window[(size_t) i] = (float) (0.42 - 0.5 * std::cos (a) + 0.08 * std::cos (2.0 * a));
    }
    fftBuf.assign ((size_t) kFFT * 2, 0.0f);
    magsOut.assign ((size_t) kFFT / 2, 0.0f);
    magsSide.assign ((size_t) kFFT / 2, 0.0f);
    magsIn.assign ((size_t) kFFT / 2, 0.0f);

    excit.assign (modes.size(), 0.0f);
    excitS.assign (modes.size(), 0.0f);
    phases.assign (modes.size(), 0.0f);
    juce::Random rng (0xD15Bu);
    for (auto& p : phases) p = rng.nextFloat() * kTwoPi;
    field.assign ((size_t) N, 0.0f);

    pixClass.assign ((size_t) kImage * kImage, 0);
    for (int y = 0; y < kImage; ++y)
        for (int x = 0; x < kImage; ++x)
        {
            const size_t pix = (size_t) (y * kImage + x);
            const float rpx = radius[pix] * half;
            const int i = x - kMargin, j = y - kMargin;
            const bool inGrid = i >= 0 && j >= 0 && i < kGS && j < kGS && inside[(size_t) (j * kGS + i)] != 0;
            const float u = (radius[pix] - 1.0f) / 0.10f;
            if (inGrid && rpx < half - 3.0f)       pixClass[pix] = 1;
            else if (inGrid && rpx < half + 1.0f)  pixClass[pix] = 2;
            else if (u > 0.06f && u < 0.72f)       pixClass[pix] = 3;
        }
    for (int i = 0; i <= kGlowLut; ++i)
    {
        const float h = kGlowLutMax * (float) i / (float) kGlowLut;
        ringLut[i] = std::exp (-h * h * 55.0f);
        coreLut[i] = std::exp (-h * h * 380.0f);
    }
    shaded.assign ((size_t) kImage * kImage, 0u);
    img = juce::Image (juce::Image::ARGB, kOut * outScale, kOut * outScale, true);
    prepared = true;
}

void WaterDish::setOutputScale (int scale)
{
    const int s = scale >= 2 ? 2 : 1;
    if (s == outScale && img.isValid())
        return;
    outScale = s;
    img = juce::Image (juce::Image::ARGB, kOut * outScale, kOut * outScale, true);
}

//==============================================================================
void WaterDish::spectrum (const float* x, float* dest, bool smooth) noexcept
{
    // AnalyserNode: Blackman window, |X|/N, smoothingTimeConstant 0.7 (per frame).
    for (int i = 0; i < kFFT; ++i)
        fftBuf[(size_t) i] = x[i] * window[(size_t) i];
    std::fill (fftBuf.begin() + kFFT, fftBuf.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform (fftBuf.data(), true);
    const float tau = smooth ? 0.7f : 0.0f;
    for (int i = 0; i < kFFT / 2; ++i)
        dest[i] = tau * dest[i] + (1.0f - tau) * (fftBuf[(size_t) i] / (float) kFFT);
}

void WaterDish::bands (const float* mags, float binHz, float* energy, float* freq) const noexcept
{
    // buildDriveBands: 256 log bands, 30 Hz .. 6 kHz, energy centroids kept.
    const float lo = std::log (30.0f), hi = std::log (6000.0f);
    float weighted[kBands];
    for (int b = 0; b < kBands; ++b)
    {
        freq[b] = std::exp (lo + (hi - lo) * ((b + 0.5f) / kBands));
        energy[b] = weighted[b] = 0.0f;
    }
    const int iMax = std::min (kFFT / 2 - 1, (int) std::floor (6000.0f / binHz));
    for (int i = std::max (1, (int) std::floor (30.0f / binHz)); i <= iMax; ++i)
    {
        const int b = (int) std::floor (((std::log (i * binHz) - lo) / (hi - lo)) * kBands);
        if (b >= 0 && b < kBands)
        {
            const float e = mags[i] * mags[i];
            energy[b] += e;
            weighted[b] += i * binHz * e;
        }
    }
    for (int b = 0; b < kBands; ++b)
        if (energy[b] > 0.0f) freq[b] = weighted[b] / energy[b];
}

float WaterDish::detectPitch (const float* mags, float binHz) const noexcept
{
    // Generator detectPitch: strongest peak, drop to /2 or /3 if a strong sub-partial
    // exists, parabolic refinement.
    const int pLo = std::max (2, (int) std::floor (60.0f / binHz));
    const int pHi = std::min (kFFT / 2 - 2, (int) std::ceil (1500.0f / binHz));
    int peakI = pLo; float peakV = 0.0f;
    for (int i = pLo; i <= pHi; ++i)
        if (mags[i] > peakV) { peakV = mags[i]; peakI = i; }
    if (peakV < 1.0e-4f)
        return 0.0f;
    auto magNear = [mags, pLo] (float bin) {
        const int b = (int) std::round (bin);
        if (b < pLo) return 0.0f;
        return std::max ({ mags[b - 1], mags[b], mags[b + 1] });
    };
    int fundI = peakI;
    for (int d = 3; d >= 2; --d)
    {
        const float cand = (float) peakI / (float) d;
        if (cand >= (float) pLo && magNear (cand) > 0.45f * peakV) { fundI = (int) std::round (cand); break; }
    }
    const float a = mags[fundI - 1], b = mags[fundI], c = mags[fundI + 1];
    const float den = a - 2.0f * b + c;
    const float delta = den != 0.0f ? 0.5f * (a - c) / den : 0.0f;
    return (fundI + clampf (delta, -1.0f, 1.0f)) * binHz;
}

void WaterDish::excite (const float* energy, const float* freqs, float peak, float* out) const noexcept
{
    // computeExcitations: damped driven oscillators, partials summed in power,
    // divided by (Q * peakAmp) so a pure tone on f_k reads 1.
    std::fill (out, out + modes.size(), 0.0f);
    if (peak <= 0.0f)
        return;
    for (size_t k = 0; k < modes.size(); ++k)
    {
        const float Q = clampf (modes[k].q, 0.35f, 500.0f);
        const float fk = baseHz * modes[k].norm;
        if (! (fk > 0.0f)) continue;
        float acc = 0.0f;
        for (int b = 0; b < kBands; ++b)
        {
            const float e = energy[b];
            if (e <= 0.0f) continue;
            const float r = freqs[b] / fk;
            const float d1 = 1.0f - r * r, d2 = r / Q;
            acc += e / (d1 * d1 + d2 * d2);
        }
        out[k] = clampf (std::sqrt (acc) / (Q * peak), 0.0f, 1.0f);
    }
}

void WaterDish::buildField() noexcept
{
    // u(x) = sum_k R_k(r) [ a_k cos(n theta) + b_k sin(n theta) ] cos-phase animated.
    // a_k: the output MID drives the classic figure; b_k: the output SIDE drives the
    // rotated partner, so stereo content visibly breaks the symmetry.
    const int N = kGS * kGS;
    std::fill (field.begin(), field.end(), 0.0f);
    float* f = field.data();
    for (size_t k = 0; k < modes.size(); ++k)
    {
        const float am = excit[k], as = modes[k].n > 0 ? excitS[k] : 0.0f;
        if (am < 0.02f && as < 0.02f) continue;
        const float c = std::cos (phases[k]) * am;
        const float s = std::cos (phases[k] + 1.2f) * as;
        const float* lut = radialLut.data() + k * (size_t) (kLut + 1);
        const float* ct = cosT.data() + (size_t) modes[k].n * N;
        const float* st = sinT.data() + (size_t) modes[k].n * N;
        const std::uint16_t* li = cellLut.data();
        if (as < 0.02f)
            for (int i = 0; i < N; ++i) f[i] += lut[li[i]] * c * ct[i];
        else
            for (int i = 0; i < N; ++i) f[i] += lut[li[i]] * (c * ct[i] + s * st[i]);
    }
}

void WaterDish::updatePalette() noexcept
{
    // getActivePalette('spectral'): accent = colour of the note, trough = a fifth up.
    const float p0 = lastTarget > 0.0f ? lastTarget : (shownPitch > 0.0f ? shownPitch : lastPitch);
    spectralColour (p0, accentRGB);
    spectralColour (p0 * 1.5f, secondRGB);
    for (int i = 0; i < 3; ++i)
        whiteRGB[i] = (std::uint8_t) std::round (accentRGB[i] + (255 - accentRGB[i]) * 0.5f);
}

//==============================================================================
void WaterDish::update (const Frame& fr) noexcept
{
    if (! prepared || fr.outMid == nullptr)
        return;
    const float dt = frameDt = clampf (fr.dt, 1.0f / 240.0f, 0.05f);
    const double sr = fr.sampleRate > 0.0 ? fr.sampleRate : 48000.0;
    const float binHz = (float) (sr / kFFT);

    // --- spectra: output mid (drives the dish), output side (stereo), input (rim) ---
    // Digital silence (the output AND input tails below -100 dBFS): after a second,
    // skip the spectral analysis entirely; the resting figure only needs its phases.
    {
        const bool silentNow = tailDb (fr.outMid, kFFT, 2048) < -100.0f
                            && (fr.inMid == nullptr || tailDb (fr.inMid, kFFT, 2048) < -100.0f);
        restingTime = silentNow ? restingTime + dt : 0.0f;
        if (isResting() && haveHistory)
        {
            const float step = dt * (0.016f * 60.0f) * (0.5f + drive);
            for (size_t k = 0; k < modes.size(); ++k)
                phases[k] = std::fmod (phases[k] + step * (0.6f + modes[k].norm * 0.12f), kTwoPi * 64.0f);
            respShown += (0.18f * resp - respShown) * kOf (dt, 0.6f);
            for (int r = 0; r < kRimBars; ++r) { rimOut[r] *= 1.0f - kOf (dt, 0.35f); rimIn[r] *= 1.0f - kOf (dt, 0.35f); }
            env *= 1.0f - kOf (dt, 0.25f);
            buildField();
            return;
        }
    }

    // The output mid drives the dish every frame; the side (stereo twist) updates at
    // half rate and the input (rim comparison only) at quarter rate.
    ++frameCount;
    spectrum (fr.outMid, magsOut.data(), haveHistory);
    if (fr.outSide != nullptr && (! haveHistory || (frameCount & 1u) == 0u)) spectrum (fr.outSide, magsSide.data(), haveHistory);
    if (fr.inMid != nullptr && (! haveHistory || (frameCount & 3u) == 0u))   spectrum (fr.inMid, magsIn.data(), haveHistory);
    haveHistory = true;

    float peakBin = 0.0f;
    for (int i = 0; i < kFFT / 2; ++i) peakBin = std::max (peakBin, magsOut[(size_t) i]);
    audioPresent = peakBin > 2.0e-3f;   // generator: ~ -54 dB strongest bin -> really playing

    bands (magsOut.data(), binHz, energyOut, bandFreq);
    bands (magsSide.data(), binHz, energySide, bandFreqS);
    bands (magsIn.data(), binHz, energyIn, bandFreqIn);

    // --- loudness, onsets, limiter (the chain's dynamics) ---
    const float db = tailDb (fr.outMid, kFFT, 2048);
    const float envTarget = clampf ((db + 48.0f) / 36.0f, 0.0f, 1.0f);   // -48 dB -> 0, -12 dB -> 1
    env += (envTarget - env) * kOf (dt, envTarget > env ? 0.02f : 0.25f);
    envSlow += (env - envSlow) * kOf (dt, 0.18f);
    burst = std::max (burst * std::exp (-dt / 0.14f), clampf ((env - envSlow) * 3.0f, 0.0f, 1.0f));
    lim += (clampf (fr.limiter, 0.0f, 1.0f) - lim) * kOf (dt, fr.limiter > lim ? 0.03f : 0.3f);

    // --- pitch on the same spectrum that drives the modes; tracked pitch resolves octaves ---
    pitch = audioPresent ? detectPitch (magsOut.data(), binHz) : 0.0f;
    if (pitch > 0.0f && fr.trackedHz > 40.0f)
    {
        const float oct = std::log2 (pitch / fr.trackedHz);
        if (std::abs (oct - std::round (oct)) < 0.08f && std::abs (std::round (oct)) >= 1.0f)
            pitch /= std::exp2 (std::round (oct));
    }
    else if (pitch <= 0.0f && audioPresent && fr.trackedHz > 40.0f)
        pitch = fr.trackedHz;

    // --- tune the dish to the TARGET note (Tune's destination), measure how close the
    //     output actually is: in tune rings crisp, off pitch smears. ---
    const float target = fr.targetHz > 40.0f ? fr.targetHz : pitch;
    match = 0.0f;
    cents = 0.0f;
    if (audioPresent && target > 0.0f && pitch > 0.0f)
    {
        const float detail = lerpf (2.4f, 0.32f, clampf ((modeOrder - 1.0f) / 17.0f, 0.0f, 1.0f));
        const float refF0 = f1 * detail;
        // On a 23 mm water dish f1 is ~1.7 Hz, so at Mode order 4 the reference dish
        // tops out near 164 Hz and the generator would clamp every sung note onto the
        // same top mode. Fold the note (by octaves) into the dish's upper two octaves
        // instead: every pitch class then locks its own mode and draws its own figure.
        const float topRef = refF0 * modes.back().norm;
        float folded = target;
        while (folded > topRef) folded *= 0.5f;
        while (folded < topRef * 0.25f) folded *= 2.0f;
        int bestK = 0; float bestErr = 1.0e30f;
        for (size_t k = 0; k < modes.size(); ++k)
        {
            const float err = std::abs (std::log ((refF0 * modes[k].norm) / folded));
            if (err < bestErr) { bestErr = err; bestK = (int) k; }
        }
        lockedK = bestK;
        // Generator: smooth retune (0.16 per 60 fps frame). A new note (> ~1 semitone
        // away) snaps so the figure and readout land on it immediately. (The
        // generator's 8 Hz floor is for plates; a water dish's base sits near 1-10 Hz.)
        const float want = clampf (target / modes[(size_t) bestK].norm, 0.25f, 4000.0f);
        if (std::abs (std::log (want / baseHz)) > 0.06f)
            baseHz = want;
        else
            baseHz = lerpf (baseHz, want, 1.0f - std::pow (1.0f - 0.16f, dt * 60.0f));
        cents = 1200.0f * std::log2 (pitch / target);
        match = resonance (pitch / target, 30.0f);   // ~0.94 at 10 c, ~0.7 at 30 c, ~0.5 at 50 c
        lastPitch = pitch;
        lastTarget = target;
        shownPitch = pitch;
    }
    else
    {
        lockedK = audioPresent ? -1 : lockedK;   // silence keeps the last figure resting
        if (! audioPresent) shownPitch = 0.0f;
    }

    // --- excitation: the generator's physics on the real output spectra ---
    if (audioPresent)
    {
        float peakE = 1.0e-12f;
        for (int b = 0; b < kBands; ++b) peakE = std::max (peakE, energyOut[b]);
        peakAmp = std::sqrt (peakE);
        excite (energyOut, bandFreq, peakAmp, excit.data());
        excite (energySide, bandFreqS, peakAmp, excitS.data());   // relative to the mid
    }
    else
    {
        // magAtHzPreview: a soft peak at the last note so the resting dish shows an
        // honest "this is what that note did" figure.
        const float pk = std::max (8.0f, lastTarget > 0.0f ? lastTarget : lastPitch);
        const float w = pk * 0.04f + 2.0f;
        float prev[kBands], freqs[kBands];
        const float lo = std::log (30.0f), hi = std::log (6000.0f);
        float peakE = 1.0e-12f;
        for (int b = 0; b < kBands; ++b)
        {
            const float f = freqs[b] = std::exp (lo + (hi - lo) * ((b + 0.5f) / kBands));
            float v = std::exp (-((f - pk) * (f - pk)) / (2.0f * w * w));
            v += 0.4f * std::exp (-((f - pk * 2.5f) * (f - pk * 2.5f)) / (2.0f * (w * 2.0f) * (w * 2.0f)));
            prev[b] = v * v;
            peakE = std::max (peakE, prev[b]);
        }
        excite (prev, freqs, std::sqrt (peakE), excit.data());
        std::fill (excitS.begin(), excitS.end(), 0.0f);
    }

    // The generator's Lock-to-pitch lift, scaled by how in tune the output is: the
    // sung note always gets its figure, and it is only fully lit when on pitch.
    if (audioPresent && lockedK >= 0)
        excit[(size_t) lockedK] = std::min (1.0f, excit[(size_t) lockedK] + 0.85f * match);

    // Onsets strike the dish: a hit excites the axisymmetric (n = 0) family.
    if (burst > 0.02f)
        for (size_t k = 0; k < modes.size(); ++k)
            if (modes[k].n == 0)
                excit[k] = std::min (1.0f, excit[k] + 0.55f * burst);

    float best = 0.0f; int bestK = 0;
    float sm = 0.0f, ss = 0.0f;
    for (size_t k = 0; k < modes.size(); ++k)
    {
        if (excit[k] > best) { best = excit[k]; bestK = (int) k; }
        sm += excit[k]; ss += excitS[k];
    }
    domK = bestK;
    resp = best;
    sideRatio = sm > 1.0e-4f ? clampf (ss / sm, 0.0f, 1.0f) : 0.0f;

    // Brightness: level-independent physics (generator) x the chain's real loudness,
    // so dynamics (and the Control compressor's evening-out) are visible. Silence
    // settles to a dim resting figure.
    const float target01 = audioPresent ? resp * (0.30f + 0.70f * env) : 0.18f * resp;
    respShown += (target01 - respShown) * kOf (dt, target01 > respShown ? 0.04f : 0.6f);

    // Rim: output level per band vs what came in (dB, relative to the input's peak).
    {
        float peakIn = 1.0e-12f;
        for (int b = 0; b < kBands; ++b) peakIn = std::max ({ peakIn, energyIn[b], energyOut[b] });
        const float refDb = 10.0f * std::log10 (peakIn);
        for (int r = 0; r < kRimBars; ++r)
        {
            const float eo = energyOut[2 * r] + energyOut[2 * r + 1];
            const float ei = energyIn[2 * r] + energyIn[2 * r + 1];
            const float lo = clampf ((10.0f * std::log10 (eo + 1.0e-14f) - refDb + 54.0f) / 54.0f, 0.0f, 1.0f);
            const float li = clampf ((10.0f * std::log10 (ei + 1.0e-14f) - refDb + 54.0f) / 54.0f, 0.0f, 1.0f);
            const float to = audioPresent ? lo : 0.0f, ti = audioPresent ? li : 0.0f;
            rimOut[r] += (to - rimOut[r]) * kOf (dt, to > rimOut[r] ? 0.03f : 0.35f);
            rimIn[r]  += (ti - rimIn[r])  * kOf (dt, ti > rimIn[r]  ? 0.03f : 0.35f);
        }
    }

    // Art-directed slow-motion phases (generator: dt 0.016 per frame at 60 fps).
    const float step = dt * (0.016f * 60.0f) * (0.5f + drive);
    for (size_t k = 0; k < modes.size(); ++k)
        phases[k] = std::fmod (phases[k] + step * (0.6f + modes[k].norm * 0.12f), kTwoPi * 64.0f);
    buildField();
    updatePalette();
}

//==============================================================================
void WaterDish::render (float glow) noexcept
{
    if (! prepared)
        return;
    const float rs = clampf (respShown, 0.0f, 1.0f);
    const float bg[3] = { (float) bgRGB[0], (float) bgRGB[1], (float) bgRGB[2] };
    const float ac[3] = { (float) accentRGB[0], (float) accentRGB[1], (float) accentRGB[2] };
    const float se[3] = { (float) secondRGB[0], (float) secondRGB[1], (float) secondRGB[2] };
    const float wh[3] = { (float) whiteRGB[0], (float) whiteRGB[1], (float) whiteRGB[2] };
    const float ringGain = (0.35f + glow * 0.7f) * rs, coreGain = (0.5f + glow * 0.5f) * rs;
    const float rimA = clampf (0.25f + glow * 0.3f + env * 0.2f, 0.0f, 1.0f);
    const float knee = 2.5f * lim;                     // limiter flattens the crests
    const float half = 0.5f * (float) kGS;
    const float lutScale = (float) kGlowLut / kGlowLutMax;
    constexpr float kAdded[3] { 255.0f, 190.0f, 92.0f };     // warm: the chain added energy
    constexpr float kRemoved[3] { 96.0f, 170.0f, 255.0f };   // cool: the chain removed energy

    auto pack = [] (float a, const float* c) noexcept -> juce::uint32
    {
        const float ca = clampf (a, 0.0f, 1.0f);
        const auto A = (juce::uint32) (255.0f * ca + 0.5f);
        const auto R = (juce::uint32) (clampf (c[0], 0.0f, 255.0f) * ca + 0.5f);
        const auto G = (juce::uint32) (clampf (c[1], 0.0f, 255.0f) * ca + 0.5f);
        const auto B = (juce::uint32) (clampf (c[2], 0.0f, 255.0f) * ca + 0.5f);
        return (A << 24) | (R << 16) | (G << 8) | B;
    };

    // --- 1. shade at kImage (the generator's renderWater, LUT glow) ---
    for (int y = 0; y < kImage; ++y)
    {
        const int j = y - kMargin;
        for (int x = 0; x < kImage; ++x)
        {
            const size_t pix = (size_t) (y * kImage + x);
            const std::uint8_t cls = pixClass[pix];
            if (cls == 0) { shaded[pix] = 0u; continue; }

            float c[3] = { 0.0f, 0.0f, 0.0f };
            float alpha = 0.0f;
            if (cls <= 2)
            {
                const float h = field[(size_t) (j * kGS + (x - kMargin))];
                const float ah = std::abs (h);
                // Surface tinted by deflection (crests limited by the limiter) ...
                const float crest = h > 0.0f ? std::min (1.0f, h / (1.0f + knee * h)) : 0.0f;
                const float trough = h < 0.0f ? std::min (1.0f, -h) : 0.0f;
                const float tint = (0.10f + 0.45f * (crest + trough)) * rs;
                // ... plus the two-tier nodal glow where h ~ 0.
                float lum = 0.0f;
                if (ah < kGlowLutMax)
                {
                    const int li = (int) (ah * lutScale);
                    lum = ringLut[li] * ringGain + coreLut[li] * coreGain;
                }
                for (int ch = 0; ch < 3; ++ch)
                    c[ch] = bg[ch] + (ac[ch] - bg[ch]) * crest * tint + (se[ch] - bg[ch]) * trough * tint + wh[ch] * lum;
                alpha = 1.0f;
                if (cls == 2)
                {
                    // The generator's 3 px accent rim stroke, and the antialiased disc edge.
                    const float rpx = radius[pix] * half;
                    const float rimCov = clampf (1.5f - std::abs (rpx - (half - 1.0f)), 0.0f, 1.0f) * rimA;
                    for (int ch = 0; ch < 3; ++ch)
                        c[ch] += (ac[ch] - c[ch]) * rimCov;
                    alpha = clampf (half - rpx + 0.5f, 0.0f, 1.0f);
                }
            }
            else
            {
                // Rim spectrum ring: output level bars; where the chain removed energy the
                // input's extent shows cool, where it added energy warm.
                const float a01 = angle01[pix] * kRimBars;
                const int bar = std::min (kRimBars - 1, (int) a01);
                const float fracA = a01 - (float) bar;
                if (fracA > 0.18f && fracA < 0.82f)
                {
                    const float len = ((radius[pix] - 1.0f) / 0.10f - 0.06f) / 0.66f;
                    const float o = rimOut[bar], in = rimIn[bar];
                    const float* rc = nullptr;
                    float pass[3];
                    if (len < std::min (o, in))  { for (int ch = 0; ch < 3; ++ch) pass[ch] = ac[ch] * 0.85f; rc = pass; alpha = 0.55f; }
                    else if (len < o)            { rc = kAdded;   alpha = 0.85f; }
                    else if (len < in)           { rc = kRemoved; alpha = 0.55f; }
                    if (rc != nullptr)
                        for (int ch = 0; ch < 3; ++ch) c[ch] = rc[ch];
                }
            }
            shaded[pix] = alpha > 0.0f ? pack (alpha, c) : 0u;
        }
    }

    // --- 2. midpoint 2x upsample into the kOut image (exact averages of packed ARGB) ---
    auto avg2 = [] (juce::uint32 a, juce::uint32 b) noexcept
    {
        return ((a >> 1) & 0x7f7f7f7fu) + ((b >> 1) & 0x7f7f7f7fu) + (a & b & 0x01010101u);
    };
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < kImage; ++y)
    {
        const juce::uint32* s0 = shaded.data() + (size_t) y * kImage;
        const juce::uint32* s1 = shaded.data() + (size_t) std::min (y + 1, kImage - 1) * kImage;
        auto* o0 = outScale == 1 ? reinterpret_cast<juce::uint32*> (bd.getLinePointer (2 * y)) : nullptr;
        auto* o1 = outScale == 1 ? reinterpret_cast<juce::uint32*> (bd.getLinePointer (2 * y + 1)) : nullptr;
        for (int x = 0; x < kImage; ++x)
        {
            const int x1 = std::min (x + 1, kImage - 1);
            const juce::uint32 a = s0[x], b = s0[x1], c = s1[x], d = s1[x1];
            const juce::uint32 ab = avg2 (a, b), ac2 = avg2 (a, c);
            const juce::uint32 abcd = avg2 (ab, avg2 (c, d));
            if (outScale == 1)
            {
                o0[2 * x] = a;   o0[2 * x + 1] = ab;
                o1[2 * x] = ac2; o1[2 * x + 1] = abcd;
            }
            else
            {
                // Retina: each midpoint pixel covers a 2x2 physical block.
                auto* q0 = reinterpret_cast<juce::uint32*> (bd.getLinePointer (4 * y));
                auto* q1 = reinterpret_cast<juce::uint32*> (bd.getLinePointer (4 * y + 1));
                auto* q2 = reinterpret_cast<juce::uint32*> (bd.getLinePointer (4 * y + 2));
                auto* q3 = reinterpret_cast<juce::uint32*> (bd.getLinePointer (4 * y + 3));
                q0[4 * x] = q0[4 * x + 1] = q1[4 * x] = q1[4 * x + 1] = a;
                q0[4 * x + 2] = q0[4 * x + 3] = q1[4 * x + 2] = q1[4 * x + 3] = ab;
                q2[4 * x] = q2[4 * x + 1] = q3[4 * x] = q3[4 * x + 1] = ac2;
                q2[4 * x + 2] = q2[4 * x + 3] = q3[4 * x + 2] = q3[4 * x + 3] = abcd;
            }
        }
    }
}
} // namespace rc
