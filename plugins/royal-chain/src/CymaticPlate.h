#pragma once

// =============================================================================
//  rc::CymaticPlate — a vibrating circular free-edge plate with sand on it
// -----------------------------------------------------------------------------
//  Pure C++ (no juce::Graphics). The plate is a superposition of free-edge
//  (Neumann) Bessel modes  J_n(alpha r) cos(n(theta - theta0 - psi)),  where the
//  alphas are zeros of J_n'. theta0 = -pi/2 puts an antinode lobe of the tonic
//  (5,1) crown on every Compass puck.
//
//  The sung note, read as a scale degree of the effective key, picks a primary +
//  overtone mode pair (kFigures). Amplitudes crossfade linearly over 250 ms, the
//  degenerate cos/sin pair lets the whole figure rotate without leaving the
//  eigenspace, and ~6k grains are Newton-projected onto the exact nodal set
//  (S = 0 of the SIGNED field) so the sand settles into ~1 px lines.
//
//  step() never allocates. Everything sized is sized in prepare().
// =============================================================================

#include <cstdint>
#include <vector>

namespace rc
{
struct PlateMode { int n, s; float alpha; };

inline constexpr int kNumModes = 22;
inline constexpr PlateMode kModes[kNumModes] = {
    { 5, 1,  6.4156f }, { 5, 2, 10.5199f }, { 5, 3, 13.9872f },
    { 9, 1, 10.7114f }, { 9, 2, 15.2867f },
    { 7, 1,  8.5778f }, { 7, 2, 12.9324f },
    { 6, 1,  7.5013f }, { 6, 2, 11.7349f },
    { 3, 2,  8.0152f }, { 3, 3, 11.3459f }, { 3, 4, 14.5858f },
    { 0, 2,  7.0156f }, { 0, 3, 10.1735f },
    { 1, 4, 11.7060f }, { 1, 5, 14.8636f },
    { 2, 3,  9.9695f }, { 2, 4, 13.1704f },
    { 4, 2,  9.2824f }, { 4, 3, 12.6819f },
    { 8, 1,  9.6474f }, { 8, 2, 14.1155f },
};

// Spokes / rings a mode draws: 2n nodal half-diameters; s-1 nodal circles for
// n >= 1 (s for the axisymmetric n = 0 family).
inline constexpr int modeSpokes (const PlateMode& m) noexcept { return 2 * m.n; }
inline constexpr int modeRings (const PlateMode& m) noexcept  { return m.n == 0 ? m.s : m.s - 1; }

struct Figure { int primary, overtone; };   // indices into kModes
extern const Figure kFigures[12];           // scale degree -> modes

struct Rng
{
    std::uint32_t s = 0x5A77A21u;
    std::uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bi() noexcept { return uni() * 2.0f - 1.0f; }
};

struct alignas(16) Cell { float s, h, invG, line; };

struct PlateDrive
{
    int degree = 0;
    float primary = 1.0f;   // fundamental weight (rich styles let the overtone lead)
    float overtone = 0.3f, resp = 0, idleFloor = 0.12f, energy = 0, pulse = 0;
    bool burst = false, voiced = false, realign = false;
    float conf = 0, tune = 0, control = 0.58f, width = 0.25f, widthMeter = 0;
};

class CymaticPlate
{
public:
    static constexpr int kGrid = 128, kMaxGrains = 8192, kMaxActive = 8;

    void prepare (int grainCount, std::uint32_t seed);   // allocates; bakes the shared tables once
    void setGrainCount (int n) noexcept;
    void step (const PlateDrive&, float dt, Rng&) noexcept;

    const Cell* cells() const noexcept { return cellData.data(); }
    float sampleBreathing (float x, float y) const noexcept;   // bilinear H (breathing field)
    float sampleStatic (float x, float y) const noexcept;      // bilinear S (signed, static)

    int grainCount() const noexcept { return numGrains; }
    const float* grainX() const noexcept      { return gx_.data(); }
    const float* grainY() const noexcept      { return gy_.data(); }
    const float* grainSize() const noexcept   { return gSize.data(); }
    const float* grainSeed() const noexcept   { return gSeed.data(); }
    const float* grainTarget() const noexcept { return gTarget.data(); }
    const float* grainLine() const noexcept   { return gLine.data(); }

    float response() const noexcept  { return respG; }   // max(resp, idle floor)
    float rotation() const noexcept  { return psi; }
    float lastRotationStep() const noexcept { return dpsi; }
    int dominantMode() const noexcept;
    int dominantN() const noexcept { return kModes[dominantMode()].n; }
    float fractionNearNodal (float thr) const noexcept;

    static double besselJ (int n, double x) noexcept;
    static double besselJp (int n, double x) noexcept;

    static constexpr float kTheta0 = -1.57079632679f;    // lobe on TUNE (screen up)
    static constexpr float kCometSeed = 0.03f;           // seed < this -> comet grain

private:
    struct Tables;
    static const Tables& tables();

    struct alignas(16) Grad { float s, gx, gy, line; };

    float amp[kNumModes] {}, phase[kNumModes] {};
    float psi = 0.0f, dpsi = 0.0f, respG = 0.0f;

    std::vector<float> raw, rawH;
    std::vector<Grad> grad;
    std::vector<Cell> cellData;

    int numGrains = 0;
    std::vector<float> gx_, gy_, gSize, gSeed, gTarget, gLine;
};
} // namespace rc
