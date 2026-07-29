#pragma once

#include <sattari_audio_core/SattariModule.h>

#include <array>
#include <cmath>
#include <vector>

// =============================================================================
//  WarbleModule — Sattari Warble's DSP, extracted so the standalone plugin and a
//  Create rack slot run the SAME tape emulation.
//
//  A modulated fractional delay (wow ~0.7 Hz + flutter ~7 Hz), tape saturation,
//  a low-shelf head bump, a ~9 kHz high-cut, hiss and sparse crackle — all
//  scaled by one Age macro so a single knob walks from clean to destroyed.
//
//  Two properties worth preserving exactly:
//    * Age = 0 is a BIT-EXACT pass-through. The delay read, head bump and
//      high-cut colour (and phase-shift) the signal even with their own trims at
//      zero, so the whole coloured path is blended back toward the raw input by
//      Age. The filter states keep running so they are warm the moment Age > 0.
//    * Saturation never boosts. tanh(x*drive)/drive has slope 1 at the origin —
//      normalising by DRIVE rather than by tanh(drive) is what keeps small
//      signals unity while hot peaks genuinely compress.
// =============================================================================

namespace sattari::dsp
{

class WarbleModule final : public SattariModule
{
public:
    const char* moduleId()    const noexcept override { return "warble"; }
    const char* displayName() const noexcept override { return "Warble"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using FP = juce::AudioParameterFloat;
        using R  = juce::NormalisableRange<float>;
        auto f = [&out, &prefix] (const char* id, const char* name, float lo, float hi, float def)
        {
            out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + id, 1), name, R (lo, hi, 0.01f), def));
        };

        // The macro. Scales wow/flutter depth, saturation drive, hiss, crackle.
        f ("age", "Age", 0.0f, 1.0f, 0.35f);

        // Per-effect trims (0..1 relative weights the Age macro multiplies).
        f ("wow",     "Wow",        0.0f, 1.0f, 0.5f);
        f ("flutter", "Flutter",    0.0f, 1.0f, 0.5f);
        f ("sat",     "Saturation", 0.0f, 1.0f, 0.5f);
        f ("tone",    "Head Bump",  0.0f, 1.0f, 0.5f);
        f ("hiss",    "Hiss",       0.0f, 1.0f, 0.4f);
        f ("crackle", "Crackle",    0.0f, 1.0f, 0.3f);

        f ("mix", "Mix", 0.0f, 1.0f, 1.0f);
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "output", 1), "Output",
                                             R (-24.0f, 12.0f, 0.1f), 0.0f));
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        // Delay line: a small centre delay (~20 ms) that the wow/flutter modulation
        // swings around. Length covers the centre plus max modulation swing plus a
        // guard so interpolation reads never go out of range.
        baseDelaySamples = (float) (0.020 * sr);
        const double maxSwing = 0.010 * sr;   // generous headroom for depth
        delayLen = (int) std::ceil (baseDelaySamples + maxSwing) + 8;
        for (auto& d : delayBuf) d.assign ((size_t) delayLen, 0.0f);

        // Tape high-cut one-pole at ~9 kHz (band-limits the wet path for warmth).
        const float fc = juce::jlimit (1000.0f, (float) (sr * 0.49), 9000.0f);
        hiCutCoeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * fc / (float) sr);

        reset();
    }

    void reset() override
    {
        for (auto& d : delayBuf) std::fill (d.begin(), d.end(), 0.0f);
        delayWrite = 0;
        wowPhase = 0.0;
        flutPhase = 0.0;
        for (auto& s : headBump) s.reset();
        hiCutState  = { { 0.0f, 0.0f } };
        hissLpState = { { 0.0f, 0.0f } };
        crackleEnv  = { { 0.0f, 0.0f } };
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (state == nullptr) return;
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0) return;

        const float age      = juce::jlimit (0.0f, 1.0f, p ("age"));
        const float wowTrim  = p ("wow");
        const float flutTrim = p ("flutter");
        const float satTrim  = p ("sat");
        const float toneTrim = p ("tone");
        const float hissTrim = p ("hiss");
        const float crackTrim= p ("crackle");
        const float mix      = juce::jlimit (0.0f, 1.0f, p ("mix"));
        const float outGain  = juce::Decibels::decibelsToGain (p ("output"));

        // Age scales every degradation. Effective amounts are age * trim, so both
        // Age=0 and a given trim=0 disable that stage. mix=0 -> pure dry.
        const float wowDepthSamp  = age * wowTrim  * (0.006f * (float) sr);   // up to ~6 ms swing
        const float flutDepthSamp = age * flutTrim * (0.0012f * (float) sr);  // up to ~1.2 ms swing
        const float satAmt        = age * satTrim;                            // 0..1 drive blend
        const float toneDb        = age * toneTrim * 6.0f;                    // up to +6 dB bump
        const float hissAmt       = age * hissTrim  * 0.004f;                 // low-level noise floor
        const float crackAmt      = age * crackTrim;                          // pop probability/level

        // Tape saturation drive — see the header note on why this cannot boost.
        const float satDrive = 1.0f + satAmt * 4.0f;

        // Head-bump low-shelf per channel (cheap to set per block).
        for (int ch = 0; ch < numCh; ++ch)
            headBump[(size_t) ch].set (sr, 120.0, toneDb);

        // LFO increments. Wow ~0.7 Hz, flutter ~7 Hz.
        const double wowInc  = 2.0 * juce::MathConstants<double>::pi * 0.7 / sr;
        const double flutInc = 2.0 * juce::MathConstants<double>::pi * 7.0 / sr;

        // Sparse crackle: expected pops per second scales with crackAmt. Convert to a
        // per-sample trigger probability.
        const float popsPerSec = crackAmt * 40.0f;
        const float popProb    = popsPerSec / (float) sr;
        const float crackleDecay = std::exp (-1.0f / (float) (0.0008 * sr));   // ~0.8 ms tail

        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& d = delayBuf[(size_t) ch];
                const float in = buffer.getReadPointer (ch)[i];

                // Write dry input into the delay line.
                d[(size_t) delayWrite] = in;

                // Per-channel modulation swing (small phase offset on the right).
                const float chSkew = (ch == 1) ? 0.5f : 0.0f;
                const float wowCh  = std::sin ((float) wowPhase  + chSkew);
                const float flutCh = std::sin ((float) flutPhase + chSkew * 2.0f);
                const float modSamp = baseDelaySamples
                                      + wowDepthSamp  * wowCh
                                      + flutDepthSamp * flutCh;

                // Fractional read behind the write head.
                double rp = (double) delayWrite - (double) modSamp;
                while (rp < 0.0) rp += delayLen;
                int r0 = (int) std::floor (rp);
                const float fr = (float) (rp - r0);
                r0 %= delayLen;
                const int r1 = (r0 + 1) % delayLen;
                float wet = d[(size_t) r0] * (1.0f - fr) + d[(size_t) r1] * fr;

                // Tape saturation (dry/wet-blended so satAmt=0 is transparent).
                const float shaped = std::tanh (wet * satDrive) / satDrive;
                wet = wet + satAmt * (shaped - wet);

                // Head bump low-shelf, then tape high-cut.
                wet = headBump[(size_t) ch].process (wet);
                hiCutState[(size_t) ch] += hiCutCoeff * (wet - hiCutState[(size_t) ch]);
                wet = hiCutState[(size_t) ch];

                // Master coloration gate — see the header note. At Age=0 this yields
                // wet == in exactly; the filter states above still run so they stay
                // warm for Age>0. Hiss and crackle are Age-scaled additive terms that
                // already vanish at Age=0, so they are added after the gate.
                wet = in + age * (wet - in);

                // Hiss: filtered white noise, kept low-level.
                const float white = rng.nextFloat() * 2.0f - 1.0f;
                hissLpState[(size_t) ch] += 0.30f * (white - hissLpState[(size_t) ch]);
                wet += hissAmt * hissLpState[(size_t) ch];

                // Crackle: sparse impulsive pops with a fast decay.
                if (crackAmt > 0.0f && rng.nextFloat() < popProb)
                {
                    const float sign = (rng.nextFloat() < 0.5f) ? -1.0f : 1.0f;
                    // Pop amplitude modest so it never swamps the signal.
                    crackleEnv[(size_t) ch] = sign * (0.08f + 0.10f * rng.nextFloat());
                }
                wet += crackAmt * crackleEnv[(size_t) ch];
                crackleEnv[(size_t) ch] *= crackleDecay;

                // Own dry/wet mix, then output trim.
                float outv = in * (1.0f - mix) + wet * mix;
                outv *= outGain;

                // Transparent soft ceiling: linear below |x| = kKnee, tanh-limited
                // above it, so ordinary-level signals pass untouched but hot output
                // stays bounded (|out| < 1). At the knee both pieces meet in value
                // and slope, so there is no discontinuity.
                constexpr float kKnee = 0.9f;
                const float ax = std::fabs (outv);
                if (ax > kKnee)
                {
                    const float over = (ax - kKnee) / (1.0f - kKnee);
                    const float lim  = kKnee + (1.0f - kKnee) * std::tanh (over);
                    outv = std::copysign (lim, outv);
                }
                buffer.getWritePointer (ch)[i] = outv;
            }

            wowPhase  += wowInc;
            flutPhase += flutInc;
            if (wowPhase  >= 2.0 * juce::MathConstants<double>::pi) wowPhase  -= 2.0 * juce::MathConstants<double>::pi;
            if (flutPhase >= 2.0 * juce::MathConstants<double>::pi) flutPhase -= 2.0 * juce::MathConstants<double>::pi;

            if (++delayWrite >= delayLen) delayWrite = 0;
        }
    }

private:
    // RBJ low-shelf (used for the tape head-bump).
    struct LowShelf
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

        void set (double fs, double f, double gainDb) noexcept
        {
            const double A   = std::pow (10.0, gainDb / 40.0);
            const double w0  = 2.0 * juce::MathConstants<double>::pi
                               * juce::jlimit (10.0, fs * 0.49, f) / fs;
            const double cw  = std::cos (w0);
            const double sw  = std::sin (w0);
            const double alpha = sw / (2.0 * 0.707);
            const double twoSqrtAalpha = 2.0 * std::sqrt (A) * alpha;
            const double a0 = (A + 1.0) + (A - 1.0) * cw + twoSqrtAalpha;
            b0 = (float) ((A * ((A + 1.0) - (A - 1.0) * cw + twoSqrtAalpha)) / a0);
            b1 = (float) ((2.0 * A * ((A - 1.0) - (A + 1.0) * cw)) / a0);
            b2 = (float) ((A * ((A + 1.0) - (A - 1.0) * cw - twoSqrtAalpha)) / a0);
            a1 = (float) ((-2.0 * ((A - 1.0) + (A + 1.0) * cw)) / a0);
            a2 = (float) (((A + 1.0) + (A - 1.0) * cw - twoSqrtAalpha) / a0);
        }

        float process (float x) noexcept
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };

    float p (const char* id) const noexcept
    {
        return cachedParameterValue (id);
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    // --- Modulated fractional delay line (wow + flutter) ---------------------
    std::array<std::vector<float>, 2> delayBuf;
    int   delayLen = 0;
    int   delayWrite = 0;
    float baseDelaySamples = 0.0f;   // centre delay the modulation swings around

    // LFO phases (shared across channels; a small per-channel offset decorrelates).
    double wowPhase = 0.0;
    double flutPhase = 0.0;

    // --- Tone filters (per channel): low-shelf head bump + one-pole high-cut --
    std::array<LowShelf, 2> headBump;
    std::array<float, 2>    hiCutState { { 0.0f, 0.0f } };
    float hiCutCoeff = 1.0f;

    // --- Hiss + crackle noise generators -------------------------------------
    juce::Random rng { 0x5A77A21 };
    std::array<float, 2> hissLpState { { 0.0f, 0.0f } };   // shape the white hiss
    std::array<float, 2> crackleEnv  { { 0.0f, 0.0f } };   // decaying pop envelope
};

} // namespace sattari::dsp
