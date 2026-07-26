#pragma once

#include <sattari_audio_core/SattariModule.h>

#include <atomic>
#include <cmath>
#include <vector>

// =============================================================================
//  ImageModule — Sattari Image's DSP, extracted so the standalone plugin and a Mix
//  rack slot run the SAME imager.
//
//  M/S based. Mid is NEVER modified, so the mono sum is preserved no matter what
//  the width controls do. The side is split into three bands by complementary
//  LR4 crossovers (low + mid + high == side exactly, mid defined by subtraction),
//  each band widened independently, then global width, optional elliptical
//  low-mono, and an optional Haas/ensemble short delay.
//
//  Bit-exact neutral: when every width is exactly 1 with no low-mono, Haas or
//  mono-audition, the side filtering is skipped entirely rather than run at unity,
//  because running it would still introduce floating-point rounding.
// =============================================================================

namespace sattari::dsp
{

class ImageModule final : public SattariModule
{
public:
    const char* moduleId()    const noexcept override { return "image"; }
    const char* displayName() const noexcept override { return "Image"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using R  = juce::NormalisableRange<float>;
        using FP = juce::AudioParameterFloat;

        // Widths: 0 = mono (no side), 1 = neutral, 2 = double the stereo spread.
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "width",     1), "Width",      R (0.0f, 2.0f, 0.01f), 1.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "widthLow",  1), "Low Width",  R (0.0f, 2.0f, 0.01f), 1.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "widthMid",  1), "Mid Width",  R (0.0f, 2.0f, 0.01f), 1.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "widthHigh", 1), "High Width", R (0.0f, 2.0f, 0.01f), 1.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "xoverLoMid", 1), "Low/Mid Xover",
            R (60.0f, 800.0f, 1.0f, 0.4f), 250.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "xoverMidHi", 1), "Mid/High Xover",
            R (1200.0f, 12000.0f, 1.0f, 0.4f), 4000.0f));
        out.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (prefix + "lowMono", 1), "Low Mono", false));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "lowMonoFreq", 1), "Low Mono Freq",
            R (40.0f, 400.0f, 1.0f, 0.4f), 120.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "haas", 1), "Haas / Ensemble", R (0.0f, 1.0f, 0.01f), 0.0f));
        out.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (prefix + "monoAudition", 1), "Mono Audition", false));
    }

    void link (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        // Haas delay ring: up to 20 ms, preallocated so process() never allocates.
        haasCap = juce::jmax (1, (int) std::ceil (sr * 0.02) + 1);
        haasRing.assign ((size_t) haasCap, 0.0f);
        reset();
    }

    void reset() override
    {
        sideLowLP.reset();
        sideHighHP.reset();
        lowMonoHP.reset();
        std::fill (haasRing.begin(), haasRing.end(), 0.0f);
        haasWrite = 0;
        corrLL = corrRR = corrLR = 0.0;
        corrCount = 0;
        corr.store (0.0f, std::memory_order_relaxed);
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (state == nullptr) return;
        const int n     = buffer.getNumSamples();
        const int numCh = buffer.getNumChannels();
        if (n <= 0 || numCh <= 0) return;

        // Mono in/out: nothing to image. Keep the correlation readout sane.
        if (numCh < 2)
        {
            corr.store (1.0f, std::memory_order_relaxed);
            return;
        }

        const float width     = p ("width");
        const float widthLow  = p ("widthLow");
        const float widthMid  = p ("widthMid");
        const float widthHigh = p ("widthHigh");
        const float loMidHz   = p ("xoverLoMid");
        const float midHiHz   = p ("xoverMidHi");
        const bool  lowMono   = p ("lowMono") > 0.5f;
        const float lowMonoHz = p ("lowMonoFreq");
        const float haasAmt   = juce::jlimit (0.0f, 1.0f, p ("haas"));
        const bool  monoAud   = p ("monoAudition") > 0.5f;

        sideLowLP.a.setLP2 (loMidHz, sr);  sideLowLP.b.setLP2 (loMidHz, sr);
        sideHighHP.a.setHP2 (midHiHz, sr); sideHighHP.b.setHP2 (midHiHz, sr);
        lowMonoHP.a.setHP2 (lowMonoHz, sr); lowMonoHP.b.setHP2 (lowMonoHz, sr);

        // Neutral fast path so width=1 everywhere is genuinely bit-exact.
        const bool neutral = (width == 1.0f && widthLow == 1.0f && widthMid == 1.0f
                              && widthHigh == 1.0f && ! lowMono && haasAmt <= 0.0f && ! monoAud);

        const int haasDelay = juce::jlimit (0, haasCap - 1,
            (int) std::lround ((double) haasAmt * sr * 0.012));

        auto* L = buffer.getWritePointer (0);
        auto* R = buffer.getWritePointer (1);

        const int corrPeriod = juce::jmax (1, (int) (sr * 0.3));

        for (int i = 0; i < n; ++i)
        {
            const float l = L[i];
            const float r = R[i];

            // M/S encode. Mid is NEVER modified -> mono sum preserved.
            const float mid  = 0.5f * (l + r);
            float       side = 0.5f * (l - r);

            if (! neutral)
            {
                const double low  = sideLowLP.process ((double) side);
                const double high = sideHighHP.process ((double) side);
                const double midB = (double) side - low - high;
                side = (float) (low * (double) widthLow
                              + midB * (double) widthMid
                              + high * (double) widthHigh);

                side *= width;

                if (lowMono)
                    side = (float) lowMonoHP.process ((double) side);

                const float delayed = haasRing[(size_t) ((haasWrite - haasDelay + haasCap) % haasCap)];
                haasRing[(size_t) haasWrite] = side;
                if (++haasWrite >= haasCap) haasWrite = 0;
                if (haasAmt > 0.0f)
                    side = side + haasAmt * (delayed - side) * 0.5f;
            }

            float outL = mid + side;
            float outR = mid - side;

            if (monoAud)
            {
                outL = mid;   // side == 0 -> pure mono, level-matched to the sum
                outR = mid;
            }

            outL = juce::jlimit (-4.0f, 4.0f, outL);
            outR = juce::jlimit (-4.0f, 4.0f, outR);

            L[i] = outL;
            R[i] = outR;

            corrLL += (double) outL * outL;
            corrRR += (double) outR * outR;
            corrLR += (double) outL * outR;
            if (++corrCount >= corrPeriod)
            {
                const double den = std::sqrt (corrLL * corrRR);
                corr.store (den > 1.0e-12 ? (float) juce::jlimit (-1.0, 1.0, corrLR / den) : 1.0f,
                            std::memory_order_relaxed);
                corrLL = corrRR = corrLR = 0.0; corrCount = 0;
            }
        }
    }

    float currentCorrelation() const noexcept { return corr.load (std::memory_order_relaxed); }

private:
    static double clampF (double f, double sr) noexcept { return juce::jlimit (20.0, sr * 0.45, f); }

    // Transposed-direct-form-II biquad (double state).
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void reset() noexcept { x1 = x2 = y1 = y2 = 0; }
        double process (double x) noexcept
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
        void setLP2 (double f, double srate) noexcept   // 2nd-order Butterworth LP
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, srate) / srate;
            const double c = std::cos (w0), s = std::sin (w0);
            const double alpha = s / (2.0 * 0.70710678118654752);
            const double a0 = 1.0 + alpha;
            b0 = ((1.0 - c) * 0.5) / a0; b1 = (1.0 - c) / a0; b2 = ((1.0 - c) * 0.5) / a0;
            a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0;
        }
        void setHP2 (double f, double srate) noexcept   // 2nd-order Butterworth HP
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, srate) / srate;
            const double c = std::cos (w0), s = std::sin (w0);
            const double alpha = s / (2.0 * 0.70710678118654752);
            const double a0 = 1.0 + alpha;
            b0 = ((1.0 + c) * 0.5) / a0; b1 = (-(1.0 + c)) / a0; b2 = ((1.0 + c) * 0.5) / a0;
            a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0;
        }
    };

    // Linkwitz-Riley 4th-order = two cascaded 2nd-order Butterworths.
    struct LR4
    {
        Biquad a, b;
        void reset() noexcept { a.reset(); b.reset(); }
        double process (double x) noexcept { return b.process (a.process (x)); }
    };

    float p (const char* id) const noexcept
    {
        auto* v = state->getRawParameterValue (pfx + id);
        return v != nullptr ? v->load() : 0.0f;
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    LR4 sideLowLP;    // low  = LP at loMid
    LR4 sideHighHP;   // high = HP at midHi
    LR4 lowMonoHP;    // elliptical low-mono: HP on the SIDE only

    std::vector<float> haasRing;
    int haasCap = 1, haasWrite = 0;

    double corrLL = 0.0, corrRR = 0.0, corrLR = 0.0;
    int corrCount = 0;
    std::atomic<float> corr { 0.0f };
};

} // namespace sattari::dsp
