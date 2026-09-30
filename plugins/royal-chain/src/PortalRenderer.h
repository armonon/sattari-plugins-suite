#pragma once

// =============================================================================
//  rc::PortalRenderer — the Compass aperture's light
// -----------------------------------------------------------------------------
//  Renders the CymaticPlate into a low-res premultiplied juce::Image the way the
//  cymatic generator does it: float accumulation buffers with additive
//  blending, a per-frame decay (trails), then a hue-preserving tonemap. Nothing
//  here issues a juce::Graphics call.
//
//    * grains     -> half into a crisp "now" layer, half into a normalised trail
//    * streaks    -> "now" only (never trailed, so they flicker like Portal's)
//    * bloom      -> 1/4-res box blur of last frame's light
//    * glow layer -> distance-estimated nodal filaments, crest/trough tint,
//                    centre breath and the luminous rim halo (half-rate rows)
//    * composite  -> tonemap LUT -> packed ARGB -> optional 2x midpoint upsample
//
//  Buffers are allocated in prepare() only; render() never allocates.
// =============================================================================

#include <JuceHeader.h>
#include "CymaticPlate.h"

#include <cstdint>
#include <vector>

namespace rc
{
struct PortalLook
{
    float glow = 0.32f, smooth = 0.44f, space = 0.22f, air = 0.42f, royal = 0.5f, clean = 0.55f, tone = 0.52f;
    float level = 0.0f, energy = 0.0f, pulse = 0.0f, sibilance = 0.0f, limFlash = 0.0f, activity = 0.3f, dragAmt = 0.0f;
    int dragNode = -1;
    float dragDirX = 0.0f, dragDirY = 0.0f, dragRadius = 0.0f, vpX = 0.0f, vpY = 0.0f;

    // Overlays folded into the field image (CoreGraphics charged ~0.18 ms each to
    // rasterise them as large anti-aliased vector shapes every frame):
    float ringAlpha = 0.0f;                    // the 1 px hot ring at R (theme::ringHot)
    float membraneFill = 0.0f;                 // additive light inside the macro polygon
    float memX[5] {}, memY[5] {};              // polygon vertices (plate units)
};

class PortalRenderer
{
public:
    static constexpr float kExtent = 1.10f;   // image covers plate [-E, E]^2
    static constexpr int kMaxStreaks = 240;

    void prepare (int computeSize, int upsample);            // allocates
    void render (const CymaticPlate&, const PortalLook&, float dt, Rng&) noexcept;

    const juce::Image& image() const noexcept { return img; }
    int computeSize() const noexcept { return S; }
    int upsample() const noexcept { return up; }
    bool isPrepared() const noexcept { return S > 0; }
    void setQuality (int ladderLevel) noexcept;
    int liveStreaks() const noexcept { return live; }

    struct Stats { float meanLuma, coverHi, blowOut; };
    Stats measure() const;                                   // tests only (allocates)

    struct Timing { double grains, streaks, bloom, glow, composite; };
    Timing lastTiming() const noexcept { return timing; }

private:
    struct alignas(16) F4 { float r, g, b, a; };
    struct Streak { float ang, r0, len, ox, oy, peakMul; bool violet; };

    void emitRows (juce::Image::BitmapData&, int y) noexcept;

    int S = 0, up = 1, B = 0, W = 0;
    std::vector<F4> trail, now, glowL, bloom, bloomTmp, bloomAcc;
    std::vector<float> colU, colGt, colBt;
    std::vector<int> colGi, colBi, x0, x1, xi0, xi1;
    std::vector<std::uint8_t> rimBin;
    std::vector<float> rimProf;
    std::vector<std::uint32_t> rowCur, rowPrev;
    std::vector<std::uint32_t> ringPix;   // (Y << 16) | X of output pixels the hot ring covers
    std::vector<std::uint8_t> ringCov;    // their anti-aliased coverage (0..255)
    std::vector<Cell> cellRow;      // glow: the two cell rows lerped once per image row
    std::vector<F4> bloomRow;       // composite: the two bloom rows lerped (and gained) once per row
    int curL = 0, curR = -1, prevL = 0, prevR = -1;

    float ringL[256] {}, coreL[256] {};
    float tmT[1024] {}, tmRatio[1024] {}, tmWhite[1024] {};
    float rimH[256] {};

    Streak streaks[kMaxStreaks] {};
    int rr = 0, live = 0;

    int ladder = 0, glowRowMod = 2, streakCap = kMaxStreaks;
    bool bloomOn = true;
    unsigned frameCounter = 0;

    juce::Image img;
    Timing timing { 0, 0, 0, 0, 0 };
};
} // namespace rc
