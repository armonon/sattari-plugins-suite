#pragma once

#include <JuceHeader.h>
#include <array>

/**
 * Sample-accurate real-time stem separator.
 *
 * Splits stereo audio into four stems in a single pass using only IIR filters,
 * envelope followers, and M/S mid-side processing — no FFT or lookahead.
 *
 * Algorithm:
 *   Bass   = steep low-pass (2nd-order Butterworth, ~200 Hz) on the mono sum.
 *   Vocals = bandpass of the mid (centre) channel: HPF ~300 Hz then LPF ~3.5 kHz.
 *            Centre content == vocals in most mixed material.
 *   Drums  = onset-gated wideband signal.  A fast envelope follower on the HPF
 *            signal detects transients; a slow follower tracks the RMS floor.
 *            When fast / slow > threshold the sample belongs to "drums".
 *   Music  = input minus bass, minus an attenuated drums contribution, minus
 *            an attenuated vocals contribution — i.e. the harmonic remainder.
 *
 * Quality caveat: this is substantially below Demucs/HTDemucs.  It works well
 * for surgical EQ use-cases (kill the kick, ride the vocals up, etc.) but will
 * bleed at transient edges and on complex material.  The AI Split button still
 * delivers the full-quality offline separation when the user has time to wait.
 */
class RealTimeStemSeparator
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /**
     * Separate @p numSamples of stereo input into four stereo output buffers.
     * All output buffers must have at least @p numSamples frames allocated.
     * Gains: each output already contains ONLY that stem — the caller applies
     * per-stem gain and mixes back.
     *
     * Lane indices match DeckPlayer::StemLane:
     *   0 = fullMix (not written — caller keeps the pre-separation signal)
     *   1 = drums
     *   2 = bass
     *   3 = music
     *   4 = vocals
     */
    void process (const float* inL, const float* inR, int numSamples,
                  float* drumsL, float* drumsR,     // lane 1
                  float* bassL,  float* bassR,      // lane 2
                  float* musicL, float* musicR,     // lane 3
                  float* vocalsL, float* vocalsR);  // lane 4

    // Tweakable thresholds (normalised 0..1)
    void setDrumsSensitivity (float s) noexcept { drumsSensitivity = juce::jlimit (0.05f, 0.99f, s); }
    void setVocalsMidWeight  (float w) noexcept { vocalsMidWeight  = juce::jlimit (0.0f,  1.0f,  w); }

private:
    // ── per-channel state ─────────────────────────────────────────────────────
    struct Chan
    {
        // Bass: 2nd-order Butterworth LP ~200 Hz (two cascaded one-poles)
        float bassZ1 = 0.0f, bassZ2 = 0.0f;

        // Vocal bandpass: HPF ~300 Hz then LPF ~3.5 kHz
        float vocHPZ = 0.0f;
        float vocLPZ = 0.0f;

        // Drum transient detection: HPF + fast/slow envelope
        float drumHPZ    = 0.0f;
        float drumFastEnv = 0.0f;
        float drumSlowEnv = 0.0001f;

        // Music: notch the centre channel (mid-side) for the side component
        float musicLPZ = 0.0f;
    };

    std::array<Chan, 2> ch {};

    // ── filter coefficients (computed in prepare()) ───────────────────────────
    float bassA  = 0.0f;   // one-pole LP coeff for bass (applied twice)
    float vocHPA = 0.0f;   // one-pole HP coeff for vocal HPF
    float vocLPA = 0.0f;   // one-pole LP coeff for vocal LPF
    float drumHPA = 0.0f;  // one-pole HP coeff for drum onset detector

    float drumFastDecay = 0.0f;   // fast envelope release
    float drumSlowAttack = 0.0f;  // slow envelope attack (smooths RMS floor)
    float drumSlowDecay  = 0.0f;

    float drumsSensitivity = 0.55f;
    float vocalsMidWeight  = 0.85f;  // how much of the mid channel to use for vocals

    double sr = 44100.0;

    static float onePoleLP (float in, float z, float a) noexcept
    {
        return a * in + (1.0f - a) * z;
    }

    static float onePoleHP (float in, float& z, float a) noexcept
    {
        const float y = a * (z + in - z);  // HP = input − LP
        // Simpler: HP = in − LP(in)
        const float lp = onePoleLP (in, z, a);
        z = lp;
        return in - lp;
    }
};
