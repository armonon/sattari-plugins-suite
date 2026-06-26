#include "RealTimeStemSeparator.h"

namespace
{
// One-pole LP coefficient for a given cut-off.
// a = 1 - exp(-2π * fc / sr)  (bilinear approx)
inline float lpCoeff (double fc, double sampleRate) noexcept
{
    const double rc = 1.0 / (juce::MathConstants<double>::twoPi * fc);
    return (float) (1.0 / (1.0 + rc * sampleRate));
}
}

void RealTimeStemSeparator::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sr = sampleRate;

    // ── Bass: 2nd-order LP (two cascaded one-poles) at ~200 Hz ───────────────
    bassA = lpCoeff (200.0, sr);

    // ── Vocal HPF ~300 Hz, LPF ~3500 Hz ─────────────────────────────────────
    vocHPA = 1.0f - lpCoeff (300.0, sr);   // HP coeff = 1 - LP coeff
    vocLPA = lpCoeff (3500.0, sr);

    // ── Drum onset HPF ~150 Hz (removes bass from transient detector) ─────────
    drumHPA = 1.0f - lpCoeff (150.0, sr);

    // Fast envelope: ~4 ms attack, ~40 ms release
    drumFastDecay  = (float) std::exp (-1.0 / (0.040 * sr));
    // Slow envelope tracks RMS floor: ~200 ms attack, 400 ms release
    drumSlowAttack = lpCoeff (1.0 / (0.200), sr);
    drumSlowDecay  = (float) std::exp (-1.0 / (0.400 * sr));

    reset();
}

void RealTimeStemSeparator::reset()
{
    ch = {};
    // Initialise slow envelope to a small non-zero value to avoid div-by-zero.
    for (auto& c : ch)
        c.drumSlowEnv = 0.0001f;
}

void RealTimeStemSeparator::process (const float* inL, const float* inR, int numSamples,
                                     float* drumsL, float* drumsR,
                                     float* bassL,  float* bassR,
                                     float* musicL, float* musicR,
                                     float* vocalsL, float* vocalsR)
{
    jassert (inL != nullptr && inR != nullptr);

    const float* inputs[2]  = { inL,    inR    };
    float* drumsOut[2]      = { drumsL, drumsR };
    float* bassOut[2]       = { bassL,  bassR  };
    float* musicOut[2]      = { musicL, musicR };
    float* vocalsOut[2]     = { vocalsL, vocalsR };

    for (int i = 0; i < numSamples; ++i)
    {
        const float lIn = inL[i];
        const float rIn = inR[i];

        // M/S decomposition
        const float mid  = (lIn + rIn) * 0.5f;
        const float side = (lIn - rIn) * 0.5f;

        for (int c = 0; c < 2; ++c)
        {
            const float x = inputs[c][i];
            auto& s = ch[(size_t) c];

            // ── BASS (2-stage LP on mono sum) ────────────────────────────────
            s.bassZ1 = bassA * mid  + (1.0f - bassA) * s.bassZ1;
            s.bassZ2 = bassA * s.bassZ1 + (1.0f - bassA) * s.bassZ2;
            const float bass = s.bassZ2;

            // ── VOCALS (bandpass of mid channel) ─────────────────────────────
            // HPF: remove sub-bass rumble
            const float midLP = vocHPA == 0.0f ? 0.0f : (1.0f - vocHPA) * s.vocHPZ + vocHPA * mid;
            const float midHP = mid - midLP;
            s.vocHPZ = midLP;
            // LPF: remove high-frequency sibilance
            const float vocalLP = vocLPA * midHP + (1.0f - vocLPA) * s.vocLPZ;
            s.vocLPZ = vocalLP;
            const float vocals = vocalLP * vocalsMidWeight;

            // ── DRUMS (onset-gated full signal) ──────────────────────────────
            // HPF the input to isolate attack transients
            const float xLP = (1.0f - drumHPA) * s.drumHPZ + drumHPA * x;
            s.drumHPZ = xLP;
            const float hpf = x - xLP;
            const float hpfAbs = std::abs (hpf);

            // Fast envelope follower: attack instant, release slow
            s.drumFastEnv = hpfAbs > s.drumFastEnv
                          ? hpfAbs                               // instant attack
                          : s.drumFastEnv * drumFastDecay;       // slow release

            // Slow envelope follower: tracks longer-term noise floor
            s.drumSlowEnv = hpfAbs > s.drumSlowEnv
                          ? drumSlowAttack * hpfAbs + (1.0f - drumSlowAttack) * s.drumSlowEnv
                          : s.drumSlowEnv * drumSlowDecay;

            // Transient mask: ratio of fast to slow, smoothed by a curve
            const float ratio = s.drumFastEnv / (s.drumSlowEnv + 1e-9f);
            // Soft gate: below threshold the mask fades to 0 smoothly
            const float threshold = 1.0f + drumsSensitivity * 6.0f;  // 1..7
            const float drumMask  = juce::jlimit (0.0f, 1.0f, (ratio - threshold) / threshold);
            const float drums = x * drumMask;

            // ── MUSIC (harmonic residual) ─────────────────────────────────────
            // Music = input minus the centre/vocal content; side channel carries
            // most instrumental spread.  We add a blend of both to retain width.
            const float sideScaled = (c == 0) ? side : -side;   // reconstruct L=mid+side, R=mid-side
            const float music = sideScaled + mid * (1.0f - vocalsMidWeight) - bass * 0.5f;

            // ── Write outputs ─────────────────────────────────────────────────
            drumsOut[c][i]  = drums;
            bassOut[c][i]   = bass;
            vocalsOut[c][i] = vocals;
            musicOut[c][i]  = music;
        }
    }
}
