#include "CymaticPlate.h"

#include <algorithm>
#include <cmath>

namespace rc
{
namespace
{
constexpr float kTwoPi = 6.28318530717959f;

inline float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float smoothstep (float a, float b, float x) noexcept
{
    const float t = clamp01 ((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

// Degree -> (primary, overtone), indices into kModes. The tonic is the 5-fold
// crown aligned with the pucks; the 5th shares its symmetry, so consonant
// degrees read as relatives of the root.
const Figure kFigures[12] = {
    {  0,  1 },   //  0 ROOT     (5,1) + (5,2)
    {  3,  4 },   //  1 MIN 2ND  (9,1) + (9,2)
    {  5,  6 },   //  2 2ND      (7,1) + (7,2)
    {  7,  8 },   //  3 MIN 3RD  (6,1) + (6,2)
    {  9, 10 },   //  4 3RD      (3,2) + (3,3)
    { 12, 13 },   //  5 4TH      (0,2) + (0,3)
    { 14, 15 },   //  6 TRITONE  (1,4) + (1,5)
    {  1,  2 },   //  7 5TH      (5,2) + (5,3)
    { 16, 17 },   //  8 MIN 6TH  (2,3) + (2,4)
    { 18, 19 },   //  9 6TH      (4,2) + (4,3)
    { 20, 21 },   // 10 MIN 7TH  (8,1) + (8,2)
    { 10, 11 },   // 11 7TH      (3,3) + (3,4)
};

//==============================================================================
// Bessel J_n by its integral representation, midpoint rule with M = 64:
//   J_n(x) = (1/pi) Int_0^pi cos(n t - x sin t) dt
// The integrand is smooth and periodic, so the midpoint rule is spectrally
// accurate over this range (every tabulated J_n' zero reproduces to 4 dp).
double CymaticPlate::besselJ (int n, double x) noexcept
{
    constexpr int M = 64;
    double sum = 0.0;
    for (int j = 0; j < M; ++j)
    {
        const double t = (j + 0.5) * 3.14159265358979323846 / M;
        sum += std::cos (n * t - x * std::sin (t));
    }
    return sum / M;
}

double CymaticPlate::besselJp (int n, double x) noexcept
{
    if (n == 0)
        return -besselJ (1, x);
    return 0.5 * (besselJ (n - 1, x) - besselJ (n + 1, x));
}

//==============================================================================
// Immutable, process-wide bake (~15 ms, ~2.8 MB of static storage). Every plate
// instance shares it; C++11 guarantees the function-local static is initialised
// exactly once, thread-safely.
struct CymaticPlate::Tables
{
    static constexpr int G = kGrid, N = kGrid * kGrid, kMaxN = 10;
    float cosN[kMaxN][N];
    float sinN[kMaxN][N];
    float radial[kNumModes][N];
    std::uint8_t inside[N];

    Tables()
    {
        static float rr[N], th[N];   // bake scratch (static storage, not stack)
        for (int j = 0; j < G; ++j)
            for (int i = 0; i < G; ++i)
            {
                const float x = -1.0f + ((float) i + 0.5f) * 2.0f / (float) G;
                const float y = -1.0f + ((float) j + 0.5f) * 2.0f / (float) G;
                const int c = j * G + i;
                rr[c] = std::sqrt (x * x + y * y);
                th[c] = std::atan2 (y, x);
                inside[c] = rr[c] <= 1.0f ? 1 : 0;
            }

        for (int n = 0; n < kMaxN; ++n)
            for (int c = 0; c < N; ++c)
            {
                cosN[n][c] = std::cos ((float) n * th[c]);
                sinN[n][c] = std::sin ((float) n * th[c]);
            }

        float lut[1025];
        for (int k = 0; k < kNumModes; ++k)
        {
            float peak = 1.0e-9f;
            for (int i = 0; i <= 1024; ++i)
            {
                lut[i] = (float) besselJ (kModes[k].n, (double) kModes[k].alpha * (double) i / 1024.0);
                peak = std::max (peak, std::abs (lut[i]));
            }
            const float invPeak = 1.0f / peak;
            for (int c = 0; c < N; ++c)
            {
                // Outside the disc the radial profile holds R(1) so gradients stay
                // continuous for grains that briefly overshoot the rim.
                const float f = std::min (rr[c], 1.0f) * 1024.0f;
                const int i0 = std::min (1023, (int) f);
                const float t = f - (float) i0;
                radial[k][c] = (lut[i0] + (lut[i0 + 1] - lut[i0]) * t) * invPeak;
            }
        }
    }
};

const CymaticPlate::Tables& CymaticPlate::tables()
{
    static const Tables t;
    return t;
}

//==============================================================================
void CymaticPlate::prepare (int grainCount, std::uint32_t seed)
{
    (void) tables();

    constexpr int N = kGrid * kGrid;
    raw.assign (N, 0.0f);
    rawH.assign (N, 0.0f);
    grad.assign (N, Grad { 0.0f, 0.0f, 0.0f, 0.0f });
    cellData.assign (N, Cell { 0.0f, 0.0f, 1.0f, 0.0f });

    gx_.assign (kMaxGrains, 0.0f);
    gy_.assign (kMaxGrains, 0.0f);
    gSize.assign (kMaxGrains, 1.0f);
    gSeed.assign (kMaxGrains, 0.5f);
    gTarget.assign (kMaxGrains, 0.0f);
    gLine.assign (kMaxGrains, 0.0f);

    // Seed every slot (so a later setGrainCount() never exposes garbage), uniform
    // in the disc by rejection.
    Rng r { seed != 0 ? seed : 0x5A77A21u };
    for (int i = 0; i < kMaxGrains; ++i)
    {
        float a, b;
        do { a = r.bi(); b = r.bi(); } while (a * a + b * b > 0.95f);
        gx_[(size_t) i] = a;
        gy_[(size_t) i] = b;
        gSize[(size_t) i] = 0.55f + 0.8f * r.uni();
        gSeed[(size_t) i] = r.uni();
    }

    std::fill (std::begin (amp), std::end (amp), 0.0f);
    std::fill (std::begin (phase), std::end (phase), 0.0f);
    psi = dpsi = respG = 0.0f;
    setGrainCount (grainCount);
}

void CymaticPlate::setGrainCount (int n) noexcept
{
    numGrains = std::max (0, std::min (kMaxGrains, n));
}

int CymaticPlate::dominantMode() const noexcept
{
    int best = 0;
    for (int k = 1; k < kNumModes; ++k)
        if (amp[k] > amp[best])
            best = k;
    return best;
}

float CymaticPlate::fractionNearNodal (float thr) const noexcept
{
    int near = 0, total = 0;
    for (int i = 0; i < numGrains; ++i)
    {
        if (gSeed[(size_t) i] < kCometSeed)
            continue;
        ++total;
        if (1.0f - gTarget[(size_t) i] < thr)
            ++near;
    }
    return total > 0 ? (float) near / (float) total : 0.0f;
}

namespace
{
template <typename Field>
float sampleCells (const std::vector<Cell>& cells, float x, float y, Field field) noexcept
{
    if (cells.empty())
        return 0.0f;
    constexpr int G = CymaticPlate::kGrid;
    float fx = (x + 1.0f) * (G * 0.5f) - 0.5f, fy = (y + 1.0f) * (G * 0.5f) - 0.5f;
    fx = std::max (0.0f, std::min ((float) G - 1.001f, fx));
    fy = std::max (0.0f, std::min ((float) G - 1.001f, fy));
    const int i0 = (int) fx, j0 = (int) fy;
    const float tx = fx - (float) i0, ty = fy - (float) j0;
    const Cell* p = cells.data() + j0 * G + i0;
    const float top = field (p[0]) + (field (p[1]) - field (p[0])) * tx;
    const float bot = field (p[G]) + (field (p[G + 1]) - field (p[G])) * tx;
    return top + (bot - top) * ty;
}
} // namespace

float CymaticPlate::sampleBreathing (float x, float y) const noexcept
{
    return sampleCells (cellData, x, y, [] (const Cell& c) noexcept { return c.h; });
}

float CymaticPlate::sampleStatic (float x, float y) const noexcept
{
    return sampleCells (cellData, x, y, [] (const Cell& c) noexcept { return c.s; });
}

//==============================================================================
void CymaticPlate::step (const PlateDrive& d, float dt, Rng& rng) noexcept
{
    if (cellData.empty())
        return;

    constexpr int G = kGrid, N = kGrid * kGrid;
    const auto& T = tables();

    dt = std::max (1.0f / 240.0f, std::min (1.0f / 20.0f, dt));
    const float k60 = dt * 60.0f;

    // 1. Amplitudes: 250 ms linear crossfades toward the degree's mode pair.
    {
        const int deg = ((d.degree % 12) + 12) % 12;
        const int p = kFigures[deg].primary, o = kFigures[deg].overtone;
        float tgt[kNumModes] {};
        tgt[p] = clamp01 (d.primary);
        tgt[o] = std::max (tgt[o], clamp01 (d.overtone));
        const float st = 4.0f * dt;
        for (int k = 0; k < kNumModes; ++k)
            amp[k] = clamp01 (amp[k] + std::max (-st, std::min (st, tgt[k] - amp[k])));
    }
    respG = std::max (d.resp, d.idleFloor);

    // 2. Rotation. The cos/sin pair of each mode is degenerate, so a rotated
    //    figure is still an eigenmode. Idle re-alignment eases the lobes back
    //    onto the pucks; otherwise WIDTH (and the width meter) spin it, only
    //    while the voice is locked.
    const int n1 = kModes[dominantMode()].n;
    if (d.realign && n1 > 0)
    {
        const float stepAngle = kTwoPi / (float) n1;
        dpsi = (std::round (psi / stepAngle) * stepAngle - psi) * (1.0f - std::exp (-dt / 2.0f));
    }
    else
    {
        dpsi = (0.03f + 0.20f * d.width + 0.30f * d.widthMeter) * clamp01 (d.resp / 0.3f) * dt;
    }
    psi += dpsi;
    if (psi >= kTwoPi) psi -= kTwoPi;      // every n is an integer: a 2 pi wrap is exact
    else if (psi < 0.0f) psi += kTwoPi;

    // 3. Field: static signed superposition (s) + breathing copy (h).
    int act[kMaxActive];
    int na = 0;
    for (int k = 0; k < kNumModes && na < kMaxActive; ++k)
        if (amp[k] > 0.01f)
            act[na++] = k;

    float C[kMaxActive], S[kMaxActive], B[kMaxActive];
    const float* R[kMaxActive];
    const float* cN[kMaxActive];
    const float* sN[kMaxActive];
    for (int j = 0; j < na; ++j)
    {
        const int k = act[j];
        const int n = kModes[k].n;
        const float ph = (float) n * (kTheta0 + psi);
        C[j] = std::cos (ph) * amp[k];
        S[j] = std::sin (ph) * amp[k];
        phase[k] += dt * kTwoPi * (0.30f + 0.04f * (float) n + 0.02f * (float) kModes[k].s) * (0.5f + d.energy);
        if (phase[k] > kTwoPi) phase[k] -= kTwoPi;
        B[j] = std::cos (phase[k]);
        R[j] = T.radial[k];
        cN[j] = T.cosN[n];
        sN[j] = T.sinN[n];
    }

    float* rs = raw.data();
    float* rh = rawH.data();
    float amax = 1.0e-4f;
    for (int c = 0; c < N; ++c)
    {
        float s = 0.0f, h = 0.0f;
        for (int j = 0; j < na; ++j)
        {
            const float phi = R[j][c] * (cN[j][c] * C[j] + sN[j][c] * S[j]);
            s += phi;
            h += phi * B[j];
        }
        rs[c] = s;
        rh[c] = h;
        if (T.inside[c] != 0)
            amax = std::max (amax, std::abs (s));
    }
    const float inv = 1.0f / amax;

    // 4. Normalised field, gradient (plate units) and the renderer's cells.
    for (int c = 0; c < N; ++c)
        rs[c] *= inv;

    Grad* gr = grad.data();
    Cell* cl = cellData.data();
    const float kc = (float) G * 0.25f, ke = (float) G * 0.5f;   // central / one-sided
    for (int j = 0; j < G; ++j)
    {
        const float* row = rs + j * G;
        const float* up = rs + (j > 0 ? j - 1 : j) * G;
        const float* dn = rs + (j < G - 1 ? j + 1 : j) * G;
        const float ky = (j > 0 && j < G - 1) ? kc : ke;
        for (int i = 0; i < G; ++i)
        {
            const int c = j * G + i;
            float ax;
            if (i > 0 && i < G - 1) ax = (row[i + 1] - row[i - 1]) * kc;
            else if (i == 0)        ax = (row[1] - row[0]) * ke;
            else                    ax = (row[i] - row[i - 1]) * ke;
            const float ay = (dn[i] - up[i]) * ky;
            const float m = std::sqrt (ax * ax + ay * ay);
            float hv = rh[c] * inv;
            hv = hv < -1.0f ? -1.0f : (hv > 1.0f ? 1.0f : hv);
            const float line = smoothstep (0.3f, 1.5f, m);
            gr[c] = Grad { row[i], ax, ay, line };
            cl[c] = Cell { row[i], hv, 1.0f / (m + 0.05f), line };
        }
    }

    // 5. Grains.
    const float damping = 0.25f + 0.6f * d.control;
    const float lockK = 0.10f + 0.14f * d.tune + 0.06f * damping;
    const float f = std::min (0.9f, respG * lockK * k60);
    const float agit = (0.020f + 0.068f * d.energy) * std::max (0.2f, std::min (1.0f, 1.1f - damping));
    const float kick = 0.048f * d.pulse;
    const float base = 0.0016f + 0.0022f * d.energy;
    const float jitter = d.voiced ? 0.004f * (1.0f - d.conf) : 0.0f;
    const float vort = clamp01 ((0.5f - d.resp) * 2.0f);
    const float cap = 0.05f * k60;

    float* X = gx_.data();
    float* Y = gy_.data();
    const float* seed = gSeed.data();
    float* tgtOut = gTarget.data();
    float* lineOut = gLine.data();

    auto locate = [] (float px, float py, int& idx, float& tx, float& ty) noexcept
    {
        float fx = (px + 1.0f) * (G * 0.5f) - 0.5f, fy = (py + 1.0f) * (G * 0.5f) - 0.5f;
        fx = fx < 0.0f ? 0.0f : (fx > (float) G - 1.001f ? (float) G - 1.001f : fx);
        fy = fy < 0.0f ? 0.0f : (fy > (float) G - 1.001f ? (float) G - 1.001f : fy);
        const int i0 = (int) fx, j0 = (int) fy;
        tx = fx - (float) i0;
        ty = fy - (float) j0;
        idx = j0 * G + i0;
    };

    for (int i = 0; i < numGrains; ++i)
    {
        float px = X[i], py = Y[i];
        const bool comet = seed[i] < kCometSeed;

        int ci; float tx, ty;
        locate (px, py, ci, tx, ty);
        const Grad* q = gr + ci;
        const float w00 = (1.0f - tx) * (1.0f - ty), w10 = tx * (1.0f - ty), w01 = (1.0f - tx) * ty, w11 = tx * ty;
        const float s  = q[0].s  * w00 + q[1].s  * w10 + q[G].s  * w01 + q[G + 1].s  * w11;
        const float ggx = q[0].gx * w00 + q[1].gx * w10 + q[G].gx * w01 + q[G + 1].gx * w11;
        const float ggy = q[0].gy * w00 + q[1].gy * w10 + q[G].gy * w01 + q[G + 1].gy * w11;
        const float A = std::min (1.0f, std::abs (s));

        if (! comet)
        {
            // Newton toward S = 0 on the SIGNED field: lands on the exact nodal
            // set (|S| would only find soft ~6 px bands).
            const float g2 = ggx * ggx + ggy * ggy + 0.02f;
            float n = f * s / g2;
            const float len = std::abs (n) * std::sqrt (g2);
            if (len > cap) n *= cap / len;
            px -= ggx * n;
            py -= ggy * n;
        }

        const float L = comet ? 0.0f : respG;
        const float sj = ((L * agit + kick) * A + base + jitter) * k60;   // big kicks on antinodes
        px += rng.bi() * sj;
        py += rng.bi() * sj;

        // Co-rotate with the figure (and the vortex swirl when unlocked).
        const float delta = dpsi + (comet ? 1.1f : 0.35f * vort) * (0.4f + 0.6f * A) * dt;
        const float nx = px - delta * py - 0.5f * delta * delta * px;
        const float ny = py + delta * px - 0.5f * delta * delta * py;
        px = nx; py = ny;

        float r = std::sqrt (px * px + py * py) + 1.0e-6f;
        float rn = r - ((comet ? 0.06f : 0.0f) + 0.10f * vort) * (0.3f + A) * dt;   // inflow when unlocked
        if (d.burst && ! comet)
            rn *= 1.0f + d.pulse * (0.35f + 0.65f * A) * (0.3f + 0.7f * rng.uni()) * 0.12f;   // shatter
        if (rn < 0.06f && (vort > 0.3f || comet))
        {
            const float an = rng.uni() * kTwoPi;
            rn = 0.90f + 0.07f * rng.uni();
            px = std::cos (an);
            py = std::sin (an);
            r = 1.0f;
        }
        if (rn > 0.975f)
        {
            rn = 1.95f - rn;
            if (rn > 0.975f) rn = 0.95f;
        }
        const float sc = rn / r;
        px *= sc;
        py *= sc;
        X[i] = px;
        Y[i] = py;

        locate (px, py, ci, tx, ty);
        const Grad* q2 = gr + ci;
        const float v00 = (1.0f - tx) * (1.0f - ty), v10 = tx * (1.0f - ty), v01 = (1.0f - tx) * ty, v11 = tx * ty;
        const float s2 = q2[0].s * v00 + q2[1].s * v10 + q2[G].s * v01 + q2[G + 1].s * v11;
        tgtOut[i] = 1.0f - std::min (1.0f, std::abs (s2));
        lineOut[i] = q2[0].line * v00 + q2[1].line * v10 + q2[G].line * v01 + q2[G + 1].line * v11;
    }
}
} // namespace rc
