#include "RealTimeStemSeparator.h"

namespace
{
constexpr double kButterworthQ = 0.70710678118654752440;  // 1/√2

// Crossover / band edges
constexpr double kBassFc   = 180.0;    // bass low-pass (LR4)
constexpr double kVocalLoFc = 220.0;   // vocal band high-pass
constexpr double kVocalHiFc = 5200.0;  // vocal band low-pass
constexpr double kDrumHpFc = 120.0;    // drum onset detector high-pass

// One-pole smoothing coefficient for a given time constant (seconds).
inline float smoothingCoeff (double seconds, double sampleRate) noexcept
{
    return (float) (1.0 - std::exp (-1.0 / (juce::jmax (1.0e-4, seconds) * sampleRate)));
}
}

// ── Biquad coefficient design (RBJ cookbook) ───────────────────────────────────

void RealTimeStemSeparator::Biquad::setLowpass (double fc, double fs, double q) noexcept
{
    const double w0    = juce::MathConstants<double>::twoPi * fc / fs;
    const double cosw0 = std::cos (w0);
    const double sinw0 = std::sin (w0);
    const double alpha = sinw0 / (2.0 * q);
    const double a0    = 1.0 + alpha;

    b0 = ((1.0 - cosw0) * 0.5) / a0;
    b1 =  (1.0 - cosw0)        / a0;
    b2 = ((1.0 - cosw0) * 0.5) / a0;
    a1 = (-2.0 * cosw0)        / a0;
    a2 = (1.0 - alpha)         / a0;
}

void RealTimeStemSeparator::Biquad::setHighpass (double fc, double fs, double q) noexcept
{
    const double w0    = juce::MathConstants<double>::twoPi * fc / fs;
    const double cosw0 = std::cos (w0);
    const double sinw0 = std::sin (w0);
    const double alpha = sinw0 / (2.0 * q);
    const double a0    = 1.0 + alpha;

    b0 = ((1.0 + cosw0) * 0.5)  / a0;
    b1 = (-(1.0 + cosw0))       / a0;
    b2 = ((1.0 + cosw0) * 0.5)  / a0;
    a1 = (-2.0 * cosw0)         / a0;
    a2 = (1.0 - alpha)          / a0;
}

// ── Lifecycle ──────────────────────────────────────────────────────────────────

void RealTimeStemSeparator::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sr = sampleRate;

    bassLP1.setLowpass  (kBassFc,    sr, kButterworthQ);
    bassLP2.setLowpass  (kBassFc,    sr, kButterworthQ);   // cascade → LR4
    vMidHP .setHighpass (kVocalLoFc, sr, kButterworthQ);
    vMidLP .setLowpass  (kVocalHiFc, sr, kButterworthQ);
    vSideHP.setHighpass (kVocalLoFc, sr, kButterworthQ);
    vSideLP.setLowpass  (kVocalHiFc, sr, kButterworthQ);

    for (auto& c : ch)
        c.drumHP.setHighpass (kDrumHpFc, sr, kButterworthQ);

    envCoeff       = smoothingCoeff (0.015, sr);   // 15 ms dominance follower
    fastRelease    = (float) std::exp (-1.0 / (0.030 * sr));   // 30 ms drum release
    slowAttack     = smoothingCoeff (0.060, sr);   // 60 ms floor attack
    slowRelease    = (float) std::exp (-1.0 / (0.300 * sr));   // 300 ms floor release
    vMaskSmooth    = smoothingCoeff (0.012, sr);   // 12 ms vocal mask glide
    drumMaskSmooth = smoothingCoeff (0.004, sr);   // 4 ms drum mask glide

    reset();
}

void RealTimeStemSeparator::reset()
{
    bassLP1.reset(); bassLP2.reset();
    vMidHP.reset();  vMidLP.reset();
    vSideHP.reset(); vSideLP.reset();
    for (auto& c : ch)
    {
        c.drumHP.reset();
        c.fastEnv = 0.0f;
        c.slowEnv = 1.0e-4f;
        c.drumMaskSm = 0.0f;
    }
    vEnvMid = vEnvSide = 0.0f;
    vMaskSm = 0.0f;
}

// ── Separation ──────────────────────────────────────────────────────────────────

void RealTimeStemSeparator::process (const float* inL, const float* inR, int numSamples,
                                     float* drumsL, float* drumsR,
                                     float* bassL,  float* bassR,
                                     float* musicL, float* musicR,
                                     float* vocalsL, float* vocalsR)
{
    jassert (inL != nullptr && inR != nullptr);

    const float drumThreshold = 1.0f + drumsSensitivity * 4.0f;   // 1.2 .. 5.0

    for (int i = 0; i < numSamples; ++i)
    {
        const float lIn = inL[i];
        const float rIn = inR[i];

        const float mid  = 0.5f * (lIn + rIn);
        const float side = 0.5f * (lIn - rIn);

        // ── BASS: LR4 low-pass of the mono sum (mono, summed into both ch) ───────
        const float bass = bassLP2.process (bassLP1.process (mid));

        // ── VOCALS: centre-dominant vocal-band content of the mid channel ────────
        const float vbMid  = vMidLP.process (vMidHP.process (mid));
        const float vbSide = vSideLP.process (vSideHP.process (side));

        vEnvMid  += envCoeff * (std::abs (vbMid)  - vEnvMid);
        vEnvSide += envCoeff * (std::abs (vbSide) - vEnvSide);

        // Power-domain centre dominance: 1 when the band is purely centred.
        const float midPow  = vEnvMid  * vEnvMid;
        const float sidePow = vEnvSide * vEnvSide;
        const float dominance = midPow / (midPow + sidePow + 1.0e-9f);
        const float vMaskRaw  = dominance * vocalsMidWeight;
        vMaskSm += vMaskSmooth * (vMaskRaw - vMaskSm);

        const float vocals = vbMid * vMaskSm;   // mono vocal estimate

        // ── Per-channel: drums (transient gate) + music (residual) ───────────────
        const float inCh[2]  = { lIn, rIn };
        float* drumsOut[2]   = { drumsL, drumsR };
        float* musicOut[2]   = { musicL, musicR };

        for (int c = 0; c < 2; ++c)
        {
            auto& s = ch[(size_t) c];

            const float hp  = inCh[c] - bass;     // everything above bass (exact)
            const float src = hp - vocals;        // non-vocal upper signal

            // Transient detection on a high-passed copy of src
            const float d   = s.drumHP.process (src);
            const float ad  = std::abs (d);

            s.fastEnv = ad > s.fastEnv ? ad : s.fastEnv * fastRelease;
            s.slowEnv = ad > s.slowEnv
                      ? s.slowEnv + slowAttack * (ad - s.slowEnv)
                      : s.slowEnv * slowRelease;
            s.slowEnv = juce::jmax (s.slowEnv, 1.0e-6f);

            const float ratio    = s.fastEnv / (s.slowEnv + 1.0e-9f);
            const float maskRaw  = juce::jlimit (0.0f, 1.0f,
                                                 (ratio - drumThreshold) / (drumThreshold * 0.5f));
            s.drumMaskSm += drumMaskSmooth * (maskRaw - s.drumMaskSm);

            const float drums = src * s.drumMaskSm;
            const float music = src - drums;      // tonal residual (exact)

            drumsOut[c][i] = drums;
            musicOut[c][i] = music;
        }

        // Bass and vocals are mono → write to both channels.
        bassL[i]   = bass;   bassR[i]   = bass;
        vocalsL[i] = vocals; vocalsR[i] = vocals;
    }
}
