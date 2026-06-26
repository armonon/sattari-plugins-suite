#pragma once

#include <JuceHeader.h>
#include <array>

/**
 * Sample-accurate real-time stem separator (v2 — exact reconstruction).
 *
 * Splits stereo audio into four stems in a single zero-latency pass using
 * biquad crossovers, center-channel (mid/side) extraction, and transient
 * onset gating.  No FFT, no lookahead, RT-safe (no allocations in process()).
 *
 * The defining property of this version is **exact reconstruction**:
 *
 *     bass + vocals + drums + music  ==  input    (per channel, sample exact)
 *
 * Each stem is carved out as a residual of the previous, so summing all four
 * at unity gain reproduces the dry signal bit-for-bit — riding the stem knobs
 * never introduces phase cancellation or level shift.  This is the property
 * that makes spectral-mask separators (djay Neural Mix, Serato Stems) sound
 * clean, and the previous v1 lacked it.
 *
 * Carving order:
 *   bass   = LR4 low-pass (~180 Hz) of the mono sum.
 *   hp     = input − bass                         (everything above bass, exact)
 *   vocals = center-dominant vocal-band content of mid(hp).  A mid/side power
 *            ratio gates the 220 Hz–5.2 kHz band of the centre channel, so
 *            lead vocals (centre-panned) are pulled while panned instruments
 *            stay behind.
 *   drums  = transient-gated (hp − vocals).  A fast/slow envelope ratio on a
 *            120 Hz high-passed signal opens a soft gate on percussive onsets.
 *   music  = (hp − vocals) − drums                (tonal residual, exact)
 *
 * Quality caveat: still well below Demucs/HTDemucs — snares bleed between the
 * drums and vocals lanes, and dense mixes smear.  But it is genuinely usable
 * for live "pull the vocal / kill the drums" moves, and the AI Split button
 * still delivers full-quality offline separation when there is time to wait.
 */
class RealTimeStemSeparator
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /**
     * Separate @p numSamples of stereo input into four stereo output buffers.
     * Every output buffer must have at least @p numSamples frames.
     *
     * Lane indices match DeckPlayer::StemLane (fullMix=0 is never written;
     * the caller keeps the pre-separation signal for that lane):
     *   1 = drums, 2 = bass, 3 = music, 4 = vocals
     */
    void process (const float* inL, const float* inR, int numSamples,
                  float* drumsL, float* drumsR,
                  float* bassL,  float* bassR,
                  float* musicL, float* musicR,
                  float* vocalsL, float* vocalsR);

    // Tunable thresholds (normalised 0..1)
    void setDrumsSensitivity (float s) noexcept { drumsSensitivity = juce::jlimit (0.05f, 0.99f, s); }
    void setVocalsMidWeight  (float w) noexcept { vocalsMidWeight  = juce::jlimit (0.0f,  1.0f,  w); }

private:
    // ── Transposed-Direct-Form-II biquad ──────────────────────────────────────
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        void reset() noexcept { z1 = z2 = 0.0; }

        void setLowpass  (double fc, double fs, double q) noexcept;
        void setHighpass (double fc, double fs, double q) noexcept;

        inline float process (float x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
    };

    // Mono-path filters (mid / side derived once per sample)
    Biquad bassLP1, bassLP2;     // LR4 low-pass on mid → bass
    Biquad vMidHP,  vMidLP;      // vocal band-pass on mid
    Biquad vSideHP, vSideLP;     // vocal band-pass on side (for dominance ratio)

    // Per-channel filters / envelopes
    struct Chan
    {
        Biquad drumHP;           // isolate snare/hat transients
        float  fastEnv = 0.0f;
        float  slowEnv = 1.0e-4f;
        float  drumMaskSm = 0.0f;
    };
    std::array<Chan, 2> ch {};

    // Mono envelopes for the vocal dominance gate
    float vEnvMid  = 0.0f;
    float vEnvSide = 0.0f;
    float vMaskSm  = 0.0f;

    // Smoothing / detection coefficients (set in prepare)
    float envCoeff      = 0.0f;   // mid/side dominance envelope smoothing
    float fastRelease   = 0.0f;
    float slowAttack    = 0.0f;
    float slowRelease   = 0.0f;
    float vMaskSmooth   = 0.0f;
    float drumMaskSmooth = 0.0f;

    float drumsSensitivity = 0.55f;
    float vocalsMidWeight  = 0.90f;

    double sr = 44100.0;
};
