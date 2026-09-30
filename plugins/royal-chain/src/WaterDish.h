#pragma once

// =============================================================================
//  rc::WaterDish — the Sattari Cymatic Generator's "Circular water dish",
//  ported to C++ and driven by what Royal Chain is actually doing to the voice.
// -----------------------------------------------------------------------------
//  The dish (faithful to the generator, src/cymatics.js + app.js):
//    buildCircularDish   64 lowest Neumann Bessel modes J_n(alpha r) {cos,sin}(n theta)
//    retuneDish          Faraday gravity-capillary dispersion (water, 23 mm, 9.4 mm)
//    buildDriveBands     256 log bands 30 Hz .. 6 kHz from a 16k Blackman FFT
//    computeExcitations  every mode a damped driven oscillator, partials summed in power
//    renderWater         crest/trough tint + exp(-55 h^2) ring + exp(-380 h^2) core
//    Spectral palette    the note octave-mapped into visible light
//
//  What drives it (every visual term is tied to a real signal, nothing is decorative):
//    output mid spectrum     -> the modes that ring (EQ, Air, De-ess, Color all show)
//    output side spectrum    -> the rotated sin(n theta) partners: mono = symmetric
//                               figure, stereo width twists it
//    target scale note       -> the mode the dish is tuned to (Tune's destination);
//    output pitch vs target  -> how cleanly it rings: in tune = crisp and bright,
//                               off pitch = smeared and dim (pitch correction snaps it in)
//    output loudness         -> surface energy (compression = steadier surface)
//    limiter activity        -> crest flattening
//    output onsets           -> a strike on the axisymmetric modes (ring pulse)
//    input vs output spectra -> the rim: output level bars, with what the chain
//                               REMOVED (cool) and ADDED (warm) per band
//
//  prepare() allocates; update()/render() never do. Message thread only.
// =============================================================================

#include <JuceHeader.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace rc
{
class WaterDish
{
public:
    static constexpr int kGS = 200;              // dish grid per side (generator: 220)
    static constexpr int kMargin = 10;           // shading covers +-1.10 dish radius (rim ring lives here)
    static constexpr int kImage = kGS + 2 * kMargin;   // 220 shaded ...
    static constexpr int kOut = 2 * kImage;             // ... midpoint-upsampled to 440 = the Compass
                                                        // field rect in logical px (blits 1:1 / 2:1)
    static constexpr int kFFTOrder = 14;         // 16384-point analysis (as the generator)
    static constexpr int kFFT = 1 << kFFTOrder;
    static constexpr int kBands = 256;
    static constexpr int kRimBars = 128;
    static constexpr int kMaxModes = 64;
    static constexpr int kMaxN = 14;

    struct Mode { int n = 0, s = 0; float alpha = 0, hz = 0, q = 0, norm = 1; };

    struct Frame
    {
        const float* outMid = nullptr;   // latest kFFT samples, oldest first
        const float* outSide = nullptr;
        const float* inMid = nullptr;
        double sampleRate = 48000.0;
        float targetHz = 0.0f;           // the scale note being sung (after Tune's snapping); 0 = none
        float trackedHz = 0.0f;          // Royal Chain's own pitch detector (fallback / octave check)
        float limiter = 0.0f;            // 0..1 limiter activity
        float dt = 1.0f / 60.0f;
    };

    void prepare();                      // allocates, bakes the mode tables
    // 1 = image is the field rect in logical px; 2 = in physical px on Retina, so the
    // editor's blit is 1:1 (allocates; call from a prepare path, never per frame).
    void setOutputScale (int scale);
    int outputScale() const noexcept { return outScale; }
    void update (const Frame&) noexcept;
    void render (float glow) noexcept;   // writes image()

    const juce::Image& image() const noexcept { return img; }
    bool isPrepared() const noexcept { return prepared; }

    // Readouts.
    const Mode& dominantMode() const noexcept { return modes[(size_t) (lockedK >= 0 ? lockedK : domK)]; }
    float dominantHz() const noexcept   { return baseHz * dominantMode().norm; }
    float response() const noexcept     { return resp; }
    float pitchHz() const noexcept      { return shownPitch; }
    float tuneMatch() const noexcept    { return match; }       // 1 = output pitch exactly on target
    float centsOff() const noexcept     { return cents; }
    float loudness() const noexcept     { return env; }
    float stereo() const noexcept       { return sideRatio; }
    bool hasAudio() const noexcept      { return audioPresent; }
    // Digital silence for > 1 s: the dish is showing its resting figure (the editor
    // can drop its frame rate; update() skips the spectral analysis).
    bool isResting() const noexcept     { return restingTime > 1.0f; }
    juce::Colour accent() const noexcept { return juce::Colour (accentRGB[0], accentRGB[1], accentRGB[2]); }

    // Generator defaults for the look in the reference shot.
    float modeOrder = 4.0f;              // "Mode order" 1..18
    float drive = 0.68f;                 // "Drive"

private:
    void spectrum (const float* x, float* magsDest, bool smooth) noexcept;
    void bands (const float* mags, float binHz, float* energy, float* freqOut) const noexcept;
    float detectPitch (const float* mags, float binHz) const noexcept;
    void excite (const float* energy, const float* freqs, float peak, float* out) const noexcept;
    void buildField() noexcept;
    void updatePalette() noexcept;

    bool prepared = false;
    std::vector<Mode> modes;
    // Separable tables: J_n(alpha r) via a per-mode radial LUT and a per-cell LUT
    // index; angular cos/sin(n theta) per cell for n = 0..kMaxN.
    static constexpr int kLut = 2048;
    std::vector<float> radialLut;        // modes x (kLut + 1), peak-normalised over the dish
    std::vector<std::uint16_t> cellLut;  // per cell radial LUT index
    std::vector<float> cosT, sinT;       // (kMaxN + 1) x cells
    std::vector<std::uint8_t> inside;
    std::vector<float> radius, angle01;  // per shaded pixel (kImage^2)
    std::vector<std::uint8_t> pixClass;  // 0 transparent, 1 dish interior, 2 dish edge, 3 rim-bar zone
    std::vector<juce::uint32> shaded;    // premultiplied ARGB, kImage^2
    static constexpr int kGlowLut = 1024;
    static constexpr float kGlowLutMax = 0.8f;   // |h| beyond this: both glow terms ~ 0
    float ringLut[kGlowLut + 1] {}, coreLut[kGlowLut + 1] {};
    unsigned frameCount = 0;
    int outScale = 1;
    float restingTime = 0.0f;
    std::vector<float> field;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, fftBuf, magsOut, magsSide, magsIn;

    float bandFreq[kBands] {}, bandFreqS[kBands] {}, bandFreqIn[kBands] {};
    float energyOut[kBands] {}, energySide[kBands] {}, energyIn[kBands] {};
    float rimOut[kRimBars] {}, rimIn[kRimBars] {};
    float peakAmp = 0.0f;

    std::vector<float> excit, excitS, phases;
    float f1 = 20.0f, baseHz = 20.0f, resp = 0.0f, respShown = 0.0f;
    float pitch = 0.0f, shownPitch = 0.0f, lastPitch = 220.0f, lastTarget = 0.0f;
    float match = 0.0f, cents = 0.0f, env = 0.0f, envSlow = 0.0f, burst = 0.0f, lim = 0.0f, sideRatio = 0.0f;
    bool audioPresent = false, haveHistory = false;
    int domK = 0, lockedK = -1;

    std::uint8_t bgRGB[3] { 4, 4, 10 }, accentRGB[3] { 244, 179, 94 }, secondRGB[3] { 255, 107, 107 }, whiteRGB[3] { 255, 244, 214 };
    float frameDt = 1.0f / 60.0f;

    juce::Image img;
};
} // namespace rc
