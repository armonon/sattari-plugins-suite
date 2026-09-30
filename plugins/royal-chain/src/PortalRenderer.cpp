#include "PortalRenderer.h"
#include "RoyalChainTheme.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rc
{
namespace
{
constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

// Grain deposit gain. Calibrated once against the M2 brightness gate (A3 mean
// disc luma in [0.06, 0.12]); see tests/royal_chain_ui_render.cpp.
constexpr float kGrainGain = 0.55f;

struct RGB { float r, g, b; };

// Light colours are linear floats taken straight from sRGB / 255 (no gamma),
// exactly like the cymatic generator's additive ImageData buffer.
constexpr RGB rgbOf (std::uint32_t argb) noexcept
{
    return { (float) ((argb >> 16) & 255u) / 255.0f,
             (float) ((argb >> 8) & 255u) / 255.0f,
             (float) (argb & 255u) / 255.0f };
}
constexpr RGB scaled (RGB c, float k) noexcept { return { c.r * k, c.g * k, c.b * k }; }

constexpr RGB kMint   = rgbOf (theme::mint);
constexpr RGB kViolet = rgbOf (theme::violet);
constexpr RGB kBright = rgbOf (theme::bright);
constexpr RGB kHot    = rgbOf (theme::hot);
constexpr RGB kGold   = rgbOf (theme::gold);

inline float aces (float x) noexcept
{
    const float v = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

inline double msNow() noexcept { return juce::Time::getMillisecondCounterHiRes(); }

inline std::uint32_t avg2 (std::uint32_t a, std::uint32_t b) noexcept
{
    return (a & b) + (((a ^ b) & 0xFEFEFEFEu) >> 1);
}

} // namespace

//==============================================================================
void PortalRenderer::prepare (int computeSize, int upsample)
{
    S = std::max (32, computeSize);
    up = upsample >= 2 ? 2 : 1;
    B = S / 4;
    W = S * up;

    const size_t n = (size_t) S * (size_t) S;
    trail.assign (n, F4 { 0, 0, 0, 0 });
    now.assign (n, F4 { 0, 0, 0, 0 });
    glowL.assign (n, F4 { 0, 0, 0, 0 });
    bloom.assign ((size_t) (B * B), F4 { 0, 0, 0, 0 });
    bloomTmp.assign ((size_t) (B * B), F4 { 0, 0, 0, 0 });
    bloomAcc.assign ((size_t) (B * B), F4 { 0, 0, 0, 0 });

    constexpr int GS = CymaticPlate::kGrid;
    colU.assign ((size_t) S, 0.0f);
    colGi.assign ((size_t) S, 0);
    colGt.assign ((size_t) S, 0.0f);
    colBi.assign ((size_t) S, 0);
    colBt.assign ((size_t) S, 0.0f);
    for (int i = 0; i < S; ++i)
    {
        colU[(size_t) i] = (((float) i + 0.5f) / (float) S * 2.0f - 1.0f) * kExtent;
        const float fx = std::max (0.0f, std::min ((float) GS - 1.001f, (colU[(size_t) i] + 1.0f) * (GS * 0.5f) - 0.5f));
        colGi[(size_t) i] = (int) fx;
        colGt[(size_t) i] = fx - (float) (int) fx;
        const float bx = std::max (0.0f, std::min ((float) B - 1.001f, ((float) i + 0.5f) / 4.0f - 0.5f));
        colBi[(size_t) i] = (int) bx;
        colBt[(size_t) i] = bx - (float) (int) bx;
    }

    x0.assign ((size_t) S, S);  x1.assign ((size_t) S, -1);
    xi0.assign ((size_t) S, S); xi1.assign ((size_t) S, -1);
    rimBin.assign (n, 0);
    rimProf.assign (n, 0.0f);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const float u = colU[(size_t) x], v = colU[(size_t) y];
            const float r = std::sqrt (u * u + v * v);
            if (r <= 1.06f)
            {
                x0[(size_t) y] = std::min (x0[(size_t) y], x);
                x1[(size_t) y] = std::max (x1[(size_t) y], x);
            }
            if (r < 1.0f)
            {
                xi0[(size_t) y] = std::min (xi0[(size_t) y], x);
                xi1[(size_t) y] = std::max (xi1[(size_t) y], x);
            }
            else
            {
                const float z = (r - 1.015f) / 0.016f;
                const size_t p = (size_t) y * (size_t) S + (size_t) x;
                rimProf[p] = std::exp (-z * z);
                const float th = std::atan2 (v, u);
                rimBin[p] = (std::uint8_t) std::max (0, std::min (255, (int) ((th + kPi) / kTwoPi * 256.0f)));
            }
        }

    for (int i = 0; i < 256; ++i)
    {
        const float dd = (float) i / 255.0f * 0.12f;
        ringL[i] = std::exp (-(dd / 0.030f) * (dd / 0.030f));
        coreL[i] = std::exp (-(dd / 0.007f) * (dd / 0.007f));
    }

    // Hue-preserving ACES on the max channel with a white roll-off: per-channel
    // ACES would turn mint pastel.
    for (int i = 0; i < 1024; ++i)
    {
        const float m = ((float) i + 0.5f) / 256.0f;
        const float T = aces (m);
        const float w = std::max (0.0f, std::min (1.0f, (m - 1.0f) / 3.0f)) * 0.8f;
        tmT[i] = T;
        tmRatio[i] = T / m * (1.0f - w);
        tmWhite[i] = w * T;
    }

    Rng r { 0x1234567u };
    for (auto& s : streaks)
    {
        s.ang = r.uni() * kTwoPi;
        s.r0 = 0.05f + 0.45f * r.uni();
        s.len = 0.15f + 0.45f * r.uni();
        s.ox = s.oy = 0.0f;
        s.peakMul = 1.0f;
        s.violet = false;
    }
    rr = 0;
    live = 0;
    frameCounter = 0;

    rowCur.assign ((size_t) S, 0u);
    rowPrev.assign ((size_t) S, 0u);
    cellRow.assign ((size_t) CymaticPlate::kGrid, Cell { 0, 0, 0, 0 });
    bloomRow.assign ((size_t) std::max (1, B), F4 { 0, 0, 0, 0 });
    img = juce::Image (juce::Image::ARGB, W, W, true, juce::SoftwareImageType());

    // The hot ring at R: a 1-logical-px anti-aliased circle at output resolution
    // (same coverage model as a vector stroke: half width + 0.5 px feather).
    ringPix.clear();
    ringCov.clear();
    {
        const float pxPerLogical = (float) W / 440.0f;
        const float centre = 0.5f * (float) W, radius = (float) W / (2.0f * kExtent), halfW = 0.5f * pxPerLogical;
        const int y0r = std::max (0, (int) (centre - radius - halfW - 2.0f)), y1r = std::min (W - 1, (int) (centre + radius + halfW + 2.0f));
        for (int Y = y0r; Y <= y1r; ++Y)
            for (int X = 0; X < W; ++X)
            {
                const float dx = (float) X + 0.5f - centre, dy = (float) Y + 0.5f - centre;
                const float cov = std::min (1.0f, halfW + 0.5f - std::abs (std::sqrt (dx * dx + dy * dy) - radius));
                if (cov > 0.004f)
                {
                    ringPix.push_back (((std::uint32_t) Y << 16) | (std::uint32_t) X);
                    ringCov.push_back ((std::uint8_t) std::lround (cov * 255.0f));
                }
            }
    }
    setQuality (ladder);
}

void PortalRenderer::setQuality (int ladderLevel) noexcept
{
    ladder = std::max (0, ladderLevel);
    glowRowMod = ladder >= 1 ? 3 : 2;
    bloomOn = ladder < 1;
    streakCap = ladder >= 2 ? 120 : kMaxStreaks;
    if (! bloomOn)
        std::fill (bloom.begin(), bloom.end(), F4 { 0, 0, 0, 0 });
}

//==============================================================================
void PortalRenderer::emitRows (juce::Image::BitmapData& bd, int y) noexcept
{
    const std::uint32_t* c = rowCur.data();
    const std::uint32_t* p = rowPrev.data();

    if (up == 1)
    {
        if (y < S && curR >= curL)
            std::memcpy (reinterpret_cast<std::uint32_t*> (bd.getLinePointer (y)) + curL,
                         c + curL, (size_t) (curR - curL + 1) * sizeof (std::uint32_t));
    }
    else
    {
        // Rows 2y (this row) and 2y-1 (midpoint of the previous and this row),
        // over the union of the two rows' spans +-1. Everything else stays 0.
        int L = std::min (curR >= curL ? curL : S, prevR >= prevL ? prevL : S) - 1;
        int R = std::max (curR >= curL ? curR : -1, prevR >= prevL ? prevR : -1) + 1;
        L = std::max (0, L);
        R = std::min (S - 1, R);
        if (L <= R)
        {
            auto* o0 = y < S ? reinterpret_cast<std::uint32_t*> (bd.getLinePointer (2 * y)) : nullptr;
            auto* om = y > 0 ? reinterpret_cast<std::uint32_t*> (bd.getLinePointer (2 * y - 1)) : nullptr;
            for (int x = L; x <= R; ++x)
            {
                const std::uint32_t cx = c[x], cn = x + 1 < S ? c[x + 1] : cx;
                const std::uint32_t px = p[x], pn = x + 1 < S ? p[x + 1] : px;
                const std::uint32_t ch = avg2 (cx, cn), ph = avg2 (px, pn);
                if (o0 != nullptr) { o0[2 * x] = cx; o0[2 * x + 1] = ch; }
                if (om != nullptr) { om[2 * x] = avg2 (px, cx); om[2 * x + 1] = avg2 (ph, ch); }
            }
        }
    }

    rowCur.swap (rowPrev);
    prevL = curL;
    prevR = curR;
}

//==============================================================================
void PortalRenderer::render (const CymaticPlate& plate, const PortalLook& lk, float dt, Rng& rng) noexcept
{
    if (S <= 0)
        return;

    const double t0 = msNow();
    const float E = kExtent;
    const float toPix = (float) S / (2.0f * E);
    const float respG = plate.response();
    const float d60 = std::min (0.94f, 0.70f + 0.20f * lk.smooth + 0.06f * lk.dragAmt);
    const float dcy = std::pow (d60, dt * 60.0f);

    F4* nw = now.data();
    F4* tr = trail.data();

    // --- 1. grains -> now (crisp) + trail (normalised echo) -----------------------
    {
        const float haze = 0.04f * (1.2f - lk.clean);
        const float sp = 0.06f + 0.12f * lk.air + 0.25f * lk.sibilance;
        const float sparkTh = 1.0f - sp;
        const float goldTh = 1.0f - sp * 0.6f * lk.royal;
        const float glowK = 0.6f + 0.45f * lk.glow;
        const float kNow = 0.5f, kTrail = 0.5f * (1.0f - dcy);
        const RGB sparkCol = scaled (kBright, 1.6f), goldCol = scaled (kGold, 1.4f), cometCol = scaled (kBright, 0.9f);

        const int nG = plate.grainCount();
        const float* X = plate.grainX();
        const float* Y = plate.grainY();
        const float* size = plate.grainSize();
        const float* seed = plate.grainSeed();
        const float* tgt = plate.grainTarget();
        const float* lin = plate.grainLine();

        for (int i = 0; i < nG; ++i)
        {
            const float T = tgt[i];
            const bool comet = seed[i] < CymaticPlate::kCometSeed;
            const float L = comet ? 0.0f : respG;
            float alpha = haze + L * T * T * glowK * (0.25f + 0.75f * lin[i]);
            alpha = alpha < 0.02f ? 0.02f : (alpha > 0.97f ? 0.97f : alpha);
            const float rad = size[i] * (0.7f + 0.85f * T);
            float w = alpha * std::max (rad * rad, 0.05f) * kGrainGain;

            const RGB* c = &kMint;
            if (comet) { c = &cometCol; w *= 1.4f; }
            else if (seed[i] > sparkTh) c = seed[i] > goldTh ? &goldCol : &sparkCol;

            const float fx = (X[i] + E) * toPix - 0.5f, fy = (Y[i] + E) * toPix - 0.5f;
            if (fx < 0.0f || fy < 0.0f)
                continue;
            const int ix = (int) fx, iy = (int) fy;
            if (ix >= S - 1 || iy >= S - 1)
                continue;
            const float tx = fx - (float) ix, ty = fy - (float) iy;
            const float w00 = (1.0f - tx) * (1.0f - ty), w10 = tx * (1.0f - ty), w01 = (1.0f - tx) * ty, w11 = tx * ty;
            const size_t o = (size_t) iy * (size_t) S + (size_t) ix;

            const float an = w * kNow, at = w * kTrail;
            F4* pn = nw + o;
            F4* pt = tr + o;
            const float ws[4] = { w00, w10, w01, w11 };
            const size_t off[4] = { 0, 1, (size_t) S, (size_t) S + 1 };
            for (int k = 0; k < 4; ++k)
            {
                const float a = ws[k] * an, b = ws[k] * at;
                F4& qn = pn[off[k]];
                F4& qt = pt[off[k]];
                qn.r += c->r * a; qn.g += c->g * a; qn.b += c->b * a;
                qt.r += c->r * b; qt.g += c->g * b; qt.b += c->b * b;
            }
        }
    }
    const double t1 = msNow();

    // --- 2. warp streaks -> now only -------------------------------------------------
    {
        const int n1 = plate.dominantN();
        const float psi = plate.rotation();
        const float act = std::max (0.30f, lk.activity);
        const int K = std::min (streakCap, (int) std::lround (160.0f * (0.30f + 0.70f * act) * (1.0f + 0.5f * lk.dragAmt)));
        live = K;
        const bool dragging = lk.dragNode >= 0;

        if (K > 0)
        {
            if (rr >= K) rr = 0;
            const int reroll = (int) std::ceil ((float) K * (0.30f + 0.15f * lk.dragAmt));
            for (int q = 0; q < reroll; ++q)
            {
                Streak& s = streaks[rr];
                rr = (rr + 1) % K;
                s.peakMul = 1.0f;
                if (dragging && rng.uni() < 0.5f)
                {
                    const float baseAng = std::atan2 (lk.dragDirY, lk.dragDirX);
                    s.ang = baseAng + rng.bi() * 0.44f;
                    s.ox = lk.dragDirX * lk.dragRadius;
                    s.oy = lk.dragDirY * lk.dragRadius;
                    s.r0 = 0.02f + 0.10f * rng.uni();
                    s.len = 0.2f + 0.6f * rng.uni();
                    s.peakMul = 1.6f;
                }
                else if (n1 > 0 && rng.uni() < 0.6f)
                {
                    // Snapped: from the plate centre, ON a nodal diameter of the figure.
                    const int m = std::min (2 * n1 - 1, (int) (rng.uni() * (float) (2 * n1)));
                    s.ang = CymaticPlate::kTheta0 + psi + (0.5f * kPi + (float) m * kPi) / (float) n1;
                    s.ox = s.oy = 0.0f;
                    s.r0 = 0.10f + 0.50f * rng.uni();
                    s.len = 0.15f + 0.60f * rng.uni();
                }
                else
                {
                    // Free: outward from the vanishing point (Portal warp).
                    s.ang = rng.uni() * kTwoPi;
                    s.ox = lk.vpX;
                    s.oy = lk.vpY;
                    s.r0 = 0.05f + 0.55f * rng.uni();
                    s.len = 0.15f + 0.75f * rng.uni();
                }
                s.violet = rng.uni() < (0.2f + 0.2f * lk.sibilance);
            }

            const float peakBase = 0.16f * (0.7f + 0.6f * lk.energy) * (1.0f - 0.45f * std::min (1.0f, respG));
            for (int q = 0; q < K; ++q)
            {
                const Streak& s = streaks[q];
                const float ca = std::cos (s.ang), sa = std::sin (s.ang);
                const float bq = s.ox * ca + s.oy * sa;
                const float cq = s.ox * s.ox + s.oy * s.oy - 0.97f * 0.97f;
                const float tmax = -bq + std::sqrt (std::max (0.0f, bq * bq - cq));
                const float r1 = std::min (s.r0 + s.len, tmax);
                if (r1 <= s.r0)
                    continue;
                const int steps = std::max (2, (int) ((r1 - s.r0) * toPix));
                const RGB& c = s.violet ? kViolet : kMint;
                // Bilinear deposits (not nearest-pixel): shallow diagonals stay
                // smooth after the 2x upsample instead of stair-stepping.
                const float pk = 1.25f / 0.7f * peakBase * s.peakMul * (s.violet ? 1.2f : 1.0f);   // 1 px steps
                const float inv = 1.0f / (float) steps;
                for (int t = 0; t <= steps; ++t)
                {
                    const float u = (float) t * inv;
                    const float prof = u < 0.4f ? u * 2.5f : (1.0f - u) * (1.0f / 0.6f);
                    const float rr2 = s.r0 + (r1 - s.r0) * u;
                    const float fx = (s.ox + ca * rr2 + E) * toPix - 0.5f;
                    const float fy = (s.oy + sa * rr2 + E) * toPix - 0.5f;
                    if (fx < 0.0f || fy < 0.0f)
                        continue;
                    const int xi = (int) fx, yi = (int) fy;
                    if (xi >= S - 1 || yi >= S - 1)
                        continue;
                    const float tx = fx - (float) xi, ty = fy - (float) yi;
                    const float w = pk * prof;
                    const float w00 = (1.0f - tx) * (1.0f - ty) * w, w10 = tx * (1.0f - ty) * w;
                    const float w01 = (1.0f - tx) * ty * w, w11 = tx * ty * w;
                    F4* p = nw + (size_t) yi * (size_t) S + (size_t) xi;
                    p[0].r += c.r * w00; p[0].g += c.g * w00; p[0].b += c.b * w00;
                    p[1].r += c.r * w10; p[1].g += c.g * w10; p[1].b += c.b * w10;
                    p[S].r += c.r * w01; p[S].g += c.g * w01; p[S].b += c.b * w01;
                    p[S + 1].r += c.r * w11; p[S + 1].g += c.g * w11; p[S + 1].b += c.b * w11;
                }
            }
        }
    }
    const double t2 = msNow();

    // --- 3. bloom (input accumulated by the previous composite: one frame of lag) ---
    if (bloomOn)
    {
        const int nb = B * B;
        for (int i = 0; i < nb; ++i)
        {
            const F4& a = bloomAcc[(size_t) i];
            bloom[(size_t) i] = F4 { a.r * 0.0625f, a.g * 0.0625f, a.b * 0.0625f, 0.0f };
            bloomAcc[(size_t) i] = F4 { 0, 0, 0, 0 };
        }
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int by = 0; by < B; ++by)
            {
                const F4* row = bloom.data() + by * B;
                F4* o = bloomTmp.data() + by * B;
                for (int bx = 0; bx < B; ++bx)
                {
                    float r = 0, g = 0, b = 0; int cnt = 0;
                    for (int k = -2; k <= 2; ++k)
                    {
                        const int xx = bx + k;
                        if (xx < 0 || xx >= B) continue;
                        r += row[xx].r; g += row[xx].g; b += row[xx].b; ++cnt;
                    }
                    const float in = 1.0f / (float) cnt;
                    o[bx] = F4 { r * in, g * in, b * in, 0.0f };
                }
            }
            for (int by = 0; by < B; ++by)
            {
                F4* o = bloom.data() + by * B;
                for (int bx = 0; bx < B; ++bx)
                {
                    float r = 0, g = 0, b = 0; int cnt = 0;
                    for (int k = -2; k <= 2; ++k)
                    {
                        const int yy = by + k;
                        if (yy < 0 || yy >= B) continue;
                        const F4& v = bloomTmp[(size_t) (yy * B + bx)];
                        r += v.r; g += v.g; b += v.b; ++cnt;
                    }
                    const float in = 1.0f / (float) cnt;
                    o[bx] = F4 { r * in, g * in, b * in, 0.0f };
                }
            }
        }
    }
    const double t3 = msNow();

    // --- 4. rim table: the figure just inside the edge --------------------------------
    // The halo shows the mode's own 2n lobes (sign of the static field: mint crests,
    // violet troughs) and breathes with H, so it never blinks out at a zero crossing
    // of the breathing phase.
    // Normalised by the rim's own static peak, so every figure's lobes reach full
    // strength at the edge (a (4,2) plate only reaches ~45 % of its interior peak
    // there; the tonic (5,1) peaks exactly at the free edge).
    {
        float rimS[256];
        float rimNorm = 0.25f;
        for (int b = 0; b < 256; ++b)
        {
            const float th = ((float) b + 0.5f) / 256.0f * kTwoPi - kPi;
            const float x = 0.985f * std::cos (th), y = 0.985f * std::sin (th);
            rimS[b] = plate.sampleStatic (x, y);
            rimH[b] = plate.sampleBreathing (x, y);
            rimNorm = std::max (rimNorm, std::abs (rimS[b]));
        }
        const float inv = 1.0f / rimNorm;
        for (int b = 0; b < 256; ++b)
            rimH[b] = (0.6f * rimS[b] + 0.4f * rimH[b]) * inv;
    }

    // --- 5. glow layer (slow-varying: one row parity per frame) ---------------------
    {
        constexpr int GS = CymaticPlate::kGrid;
        const Cell* cd = plate.cells();
        const float ringK = (0.05f + 0.10f * lk.glow) * respG;
        const float coreK = (0.10f + 0.25f * lk.glow) * respG;
        const float tintA = 0.02f * respG, tintB = 0.14f * respG;
        const float toneA = 0.6f + 0.8f * lk.tone, toneB = 1.4f - 0.8f * lk.tone;
        const float breathK = 0.02f + 0.03f * lk.glow + 0.05f * (lk.level + lk.pulse);
        const float halo = (0.18f + 0.72f * lk.level) * (0.4f + 0.6f * respG) * (1.0f + 1.5f * lk.pulse);
        const float dScale = 255.0f / 0.12f;

        const RGB cm = kMint, cv = kViolet;
        const RGB cbr = scaled (kBright, ringK), cho = scaled (kHot, coreK);

        const int mod = std::max (1, glowRowMod);
        for (int y = (int) (frameCounter % (unsigned) mod); y < S; y += mod)
        {
            const int xa = x0[(size_t) y], xb = x1[(size_t) y];
            if (xb < xa)
                continue;
            const int ia = xi0[(size_t) y], ib = xi1[(size_t) y];
            const float v = colU[(size_t) y];
            F4* __restrict gl = glowL.data() + (size_t) y * (size_t) S;
            const size_t rowOff = (size_t) y * (size_t) S;

            // Bilinear = lerp the two cell rows once per image row, then one lerp
            // per pixel (half the loads and multiplies of a 2x2 fetch).
            Cell* __restrict cr = cellRow.data();
            if (ib >= ia)
            {
                const float gty = colGt[(size_t) y];
                const Cell* r0 = cd + colGi[(size_t) y] * GS;
                const Cell* r1 = r0 + GS;
                const int g0 = colGi[(size_t) ia], g1 = std::min (GS - 1, colGi[(size_t) ib] + 1);
                for (int i = g0; i <= g1; ++i)
                    cr[i] = Cell { r0[i].s + (r1[i].s - r0[i].s) * gty, r0[i].h + (r1[i].h - r0[i].h) * gty,
                                   r0[i].invG + (r1[i].invG - r0[i].invG) * gty, r0[i].line + (r1[i].line - r0[i].line) * gty };
            }

            for (int x = xa; x <= xb; ++x)
            {
                if (x >= ia && x <= ib)
                {
                    const Cell& c0 = cr[colGi[(size_t) x]];
                    const Cell& c1 = cr[colGi[(size_t) x] + 1];
                    const float gtx = colGt[(size_t) x];
                    const float s  = c0.s + (c1.s - c0.s) * gtx;
                    const float h  = c0.h + (c1.h - c0.h) * gtx;
                    const float ig = c0.invG + (c1.invG - c0.invG) * gtx;
                    const float lw = c0.line + (c1.line - c0.line) * gtx;

                    // Distance-estimated filament: d = |s| / (|grad s| + 0.05), gated by
                    // "lineness" so a flat dead zone (r^5 at the centre) never blooms.
                    int di = (int) (std::abs (s) * ig * dScale);
                    di = di > 255 ? 255 : di;
                    const float rg = ringL[di] * lw, cc = coreL[di] * lw;

                    const float crest = h > 0.0f ? h : 0.0f, trough = h < 0.0f ? -h : 0.0f;
                    const float tint = tintA + tintB * (crest + trough);
                    const float u = colU[(size_t) x];
                    float qq = 1.0f - (u * u + v * v);
                    qq = qq * qq * breathK;
                    const float km = crest * tint * toneA + qq, kv = trough * tint * toneB;
                    gl[x] = F4 { cm.r * km + cv.r * kv + cbr.r * rg + cho.r * cc,
                                 cm.g * km + cv.g * kv + cbr.g * rg + cho.g * cc,
                                 cm.b * km + cv.b * kv + cbr.b * rg + cho.b * cc, 0.0f };
                }
                else
                {
                    const size_t p = rowOff + (size_t) x;
                    const float prof = rimProf[p];
                    if (prof > 0.01f)
                    {
                        const float hv = rimH[rimBin[p]];
                        const RGB& c = hv >= 0.0f ? cm : cv;
                        const float w = std::abs (hv) * halo * prof;
                        gl[x] = F4 { c.r * w, c.g * w, c.b * w, 0.0f };
                    }
                    else
                    {
                        gl[x] = F4 { 0, 0, 0, 0 };
                    }
                }
            }
        }
    }
    const double t4 = msNow();

    // --- 6. composite -> tonemap -> packed premultiplied ARGB (+ 2x upsample) --------
    {
        // Exposure: GLOW sets it, the limiter flashes it, and a transient kicks it.
        const float Ex = (0.85f + 0.5f * lk.glow) * (1.0f + 0.35f * lk.limFlash + 0.3f * lk.pulse);
        const float bG = bloomOn ? (0.05f + 0.25f * lk.space) * Ex : 0.0f;
        F4* bacc = bloomAcc.data();
        const F4* bl = bloom.data();

        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        jassert (bd.pixelStride == 4);

        std::fill (rowPrev.begin(), rowPrev.end(), 0u);
        prevL = S; prevR = -1;

        // Macro polygon (star-shaped about the centre) in compute pixels.
        float mvx[5], mvy[5];
        for (int i = 0; i < 5; ++i)
        {
            mvx[i] = (lk.memX[i] + E) * toPix - 0.5f;
            mvy[i] = (lk.memY[i] + E) * toPix - 0.5f;
        }
        const float memFill = lk.membraneFill;

        for (int y = 0; y < S; ++y)
        {
            // Scanline intervals of the polygon on this row (<= 2 for a pentagon).
            float m0 = 1.0e9f, m1 = -1.0e9f, m2 = 1.0e9f, m3 = -1.0e9f;
            if (memFill > 0.0f)
            {
                float xs[5];
                int nx = 0;
                const float fy = (float) y;
                for (int i = 0; i < 5; ++i)
                {
                    const int j = (i + 1) % 5;
                    const float ya = mvy[i], yb = mvy[j];
                    if ((ya <= fy && fy < yb) || (yb <= fy && fy < ya))
                        xs[nx++] = mvx[i] + (fy - ya) * (mvx[j] - mvx[i]) / (yb - ya);
                }
                std::sort (xs, xs + nx);
                if (nx >= 2) { m0 = xs[0]; m1 = xs[1]; }
                if (nx >= 4) { m2 = xs[2]; m3 = xs[3]; }
            }

            std::uint32_t* cur = rowCur.data();
            std::memset (cur, 0, (size_t) S * sizeof (std::uint32_t));
            const int xa = x0[(size_t) y], xb = x1[(size_t) y];
            curL = xa; curR = xb;

            if (xb >= xa)
            {
                const F4* __restrict gl = glowL.data() + (size_t) y * (size_t) S;
                F4* __restrict ba = bacc + (y >> 2) * B;
                F4* __restrict trow = tr + (size_t) y * (size_t) S;
                F4* __restrict nrow = nw + (size_t) y * (size_t) S;
                F4* __restrict brow = bloomRow.data();
                if (bloomOn)
                {
                    // Pre-scaled, row-interpolated bloom: one lerp per pixel.
                    const float bty = colBt[(size_t) y];
                    const F4* b0 = bl + colBi[(size_t) y] * B;
                    const F4* b1 = b0 + B;
                    const int q0 = colBi[(size_t) xa], q1 = std::min (B - 1, colBi[(size_t) xb] + 1);
                    for (int i = q0; i <= q1; ++i)
                        brow[i] = F4 { (b0[i].r + (b1[i].r - b0[i].r) * bty) * bG, (b0[i].g + (b1[i].g - b0[i].g) * bty) * bG,
                                       (b0[i].b + (b1[i].b - b0[i].b) * bty) * bG, 0.0f };
                }

                for (int x = xa; x <= xb; ++x)
                {
                    F4& t = trow[x];
                    F4& n = nrow[x];
                    const float sr = t.r + n.r, sg = t.g + n.g, sb = t.b + n.b;

                    float cr = sr * Ex + gl[x].r, cg = sg * Ex + gl[x].g, cb = sb * Ex + gl[x].b;
                    const float fx = (float) x;
                    if ((fx >= m0 && fx < m1) || (fx >= m2 && fx < m3))
                    {
                        cr += memFill; cg += memFill; cb += memFill;
                    }
                    if (bloomOn)
                    {
                        const float btx = colBt[(size_t) x];
                        const F4& q0 = brow[colBi[(size_t) x]];
                        const F4& q1 = brow[colBi[(size_t) x] + 1];
                        cr += q0.r + (q1.r - q0.r) * btx;
                        cg += q0.g + (q1.g - q0.g) * btx;
                        cb += q0.b + (q1.b - q0.b) * btx;
                        F4& acc = ba[x >> 2];
                        acc.r += sr; acc.g += sg; acc.b += sb;
                    }

                    const float m = std::max (cr, std::max (cg, cb));
                    int ti = (int) (m * 256.0f);
                    ti = ti < 0 ? 0 : (ti > 1023 ? 1023 : ti);
                    const float A0 = tmT[ti], ratio = tmRatio[ti], wt = tmWhite[ti];
                    // The clamp to alpha is mandatory: past the LUT's domain the packed
                    // channels would overflow (magenta sand piles at the rim).
                    const auto R_ = (std::uint32_t) (std::min (A0, cr * ratio + wt) * 255.0f + 0.5f);
                    const auto G_ = (std::uint32_t) (std::min (A0, cg * ratio + wt) * 255.0f + 0.5f);
                    const auto B_ = (std::uint32_t) (std::min (A0, cb * ratio + wt) * 255.0f + 0.5f);
                    const auto A_ = (std::uint32_t) (A0 * 255.0f + 0.5f);
                    cur[x] = (A_ << 24) | (R_ << 16) | (G_ << 8) | B_;   // JUCE ARGB (BGRA in memory), premultiplied

                    t.r *= dcy; t.g *= dcy; t.b *= dcy;
                    n = F4 { 0, 0, 0, 0 };
                }
            }
            emitRows (bd, y);
        }

        // Flush the final (odd) output row.
        std::memset (rowCur.data(), 0, (size_t) S * sizeof (std::uint32_t));
        curL = S; curR = -1;
        emitRows (bd, S);

        // The hot ring, premultiplied-over the finished pixels (every pixel it
        // touches lies inside the spans, so it is rewritten every frame).
        if (lk.ringAlpha > 0.002f)
        {
            constexpr std::uint32_t rr8 = (theme::ringHot >> 16) & 255u, rg8 = (theme::ringHot >> 8) & 255u, rb8 = theme::ringHot & 255u;
            const float aScale = std::min (1.0f, lk.ringAlpha) * (256.0f / 255.0f);
            const size_t n = ringPix.size();
            for (size_t k = 0; k < n; ++k)
            {
                const std::uint32_t Y = ringPix[k] >> 16, X = ringPix[k] & 0xffffu;
                auto* px = reinterpret_cast<std::uint32_t*> (bd.getLinePointer ((int) Y)) + X;
                const std::uint32_t a = (std::uint32_t) ((float) ringCov[k] * aScale);   // 0..256
                const std::uint32_t ia = 256u - a, v = *px;
                const std::uint32_t oa = (255u * a + (v >> 24) * ia) >> 8;
                const std::uint32_t orr = (rr8 * a + ((v >> 16) & 255u) * ia) >> 8;
                const std::uint32_t og = (rg8 * a + ((v >> 8) & 255u) * ia) >> 8;
                const std::uint32_t ob = (rb8 * a + (v & 255u) * ia) >> 8;
                *px = (oa << 24) | (orr << 16) | (og << 8) | ob;
            }
        }
    }
    const double t5 = msNow();

    ++frameCounter;
    timing = Timing { t1 - t0, t2 - t1, t3 - t2, t4 - t3, t5 - t4 };
}

//==============================================================================
PortalRenderer::Stats PortalRenderer::measure() const
{
    Stats st { 0.0f, 0.0f, 0.0f };
    if (S <= 0 || ! img.isValid())
        return st;

    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    double sum = 0.0;
    long nDisc = 0, nHi = 0, nBlow = 0;
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const float u = colU[(size_t) x], v = colU[(size_t) y];
            if (u * u + v * v > 0.97f * 0.97f)
                continue;
            const auto* p = bd.getPixelPointer (x * up, y * up);   // B, G, R, A in memory
            const float b = p[0], g = p[1], r = p[2];
            const float L = (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
            sum += L;
            ++nDisc;
            if (L > 0.5f) ++nHi;
            if (r > 245.0f && g > 245.0f && b > 245.0f) ++nBlow;
        }
    if (nDisc > 0)
    {
        st.meanLuma = (float) (sum / (double) nDisc);
        st.coverHi = (float) nHi / (float) nDisc;
        st.blowOut = (float) nBlow / (float) nDisc;
    }
    return st;
}
} // namespace rc
