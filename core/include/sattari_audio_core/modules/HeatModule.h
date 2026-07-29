#pragma once

#include <sattari_audio_core/SattariModule.h>

#include <atomic>
#include <cmath>
#include <memory>

// =============================================================================
//  HeatModule — Sattari Heat's DSP, extracted so the standalone plugin and a Mix
//  rack slot run the SAME saturator.
//
//  Signal path: tone-tilt shelf pair (pre-shaper) -> 4x oversampled nonlinearity
//  (one of four models, with bias asymmetry) -> DC block -> parallel dry/wet mix
//  with the dry delayed to match the oversampler -> output trim -> soft ceiling.
//
//  Only the nonlinearity is oversampled — that is where aliasing is generated —
//  and the dry path for the parallel mix is delayed by the oversampler's latency
//  so wet and dry stay phase-aligned. That latency is fixed once prepare() has
//  run, so latencySamples() simply reports it and the rack sums its slots.
// =============================================================================

namespace sattari::dsp
{

class HeatModule final : public SattariModule
{
public:
    const char* moduleId()    const noexcept override { return "heat"; }
    const char* displayName() const noexcept override { return "Heat"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using R = juce::NormalisableRange<float>;
        using P = juce::AudioParameterFloat;

        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "model", 1), "Model",
            juce::StringArray { "Tube", "Tape", "Transformer", "Transistor" }, 0));
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "drive", 1),  "Drive",
            R (0.0f, 36.0f, 0.1f), 0.0f));
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "output", 1), "Output",
            R (-24.0f, 12.0f, 0.1f), 0.0f));
        // Tone tilt: -1 dark (boost low / cut high), +1 bright (cut low / boost high).
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "tone", 1),   "Tone",
            R (-1.0f, 1.0f, 0.01f), 0.0f));
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "mix", 1),    "Mix",
            R (0.0f, 1.0f, 0.01f), 1.0f));
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "bias", 1),   "Bias",
            R (-1.0f, 1.0f, 0.01f), 0.0f));
        out.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID (prefix + "bypass", 1), "Bypass", false));
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        // DC-blocker pole scaled with sample rate so the corner stays ~5 Hz.
        const float dcR = std::exp (-2.0f * juce::MathConstants<float>::pi * 5.0f / (float) sr);
        for (auto& d : dcBlock) d.r = dcR;

        osChannels = juce::jlimit (1, 2, juce::jmax (1, numChannels));
        const int block = juce::jmax (1, maxBlockSize);

        // useIntegerLatency=true so the reported latency is a whole number of samples
        // and the dry parallel-mix ring lines up exactly (no fractional skew).
        driveOS = std::make_unique<juce::dsp::Oversampling<float>> (
                      (size_t) osChannels, 2,
                      juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        driveOS->initProcessing ((size_t) block);

        dryStash.setSize (osChannels, block);
        dryRingLen = juce::jmax (1, (int) std::lround (driveOS->getLatencyInSamples()));
        dryRing.setSize (osChannels, dryRingLen);

        reset();
    }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            lowShelf[(size_t) ch].reset();
            highShelf[(size_t) ch].reset();
            dcBlock[(size_t) ch].reset();
        }
        if (driveOS != nullptr) driveOS->reset();
        dryStash.clear();
        dryRing.clear();
        dryRingPos = 0;
        driveMeter.store (0.0f, std::memory_order_relaxed);
    }

    // Fixed once prepare() has run: the oversampler's group delay, which the dry
    // path is delayed by to keep the parallel mix phase-aligned.
    int latencySamples() const noexcept override { return dryRingLen; }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (state == nullptr) return;
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples <= 0 || numChannels <= 0) return;

        if (p ("bypass") > 0.5f) return;   // leave input untouched

        const int   model    = juce::jlimit (0, 3, (int) std::round (p ("model")));
        const float driveDb  = p ("drive");
        const float outGain  = dbToGain (p ("output"));
        const float tone     = juce::jlimit (-1.0f, 1.0f, p ("tone"));
        const float mix      = juce::jlimit (0.0f, 1.0f, p ("mix"));
        const float bias     = juce::jlimit (-1.0f, 1.0f, p ("bias"));

        const float drivePre = dbToGain (driveDb);
        // The shapers asymptote to ~+-1 however hard they are driven — drive adds
        // HARMONICS, it does not multiply level — so the wet path must NOT be divided
        // by the pre-gain (that collapses output ~15-20x at high drive). This is a
        // gentle bounded trim: exactly unity at drive=0, backing off only slightly as
        // drive climbs, to tame the RMS bump as a sine squares up.
        const float driveComp = juce::jmax (0.7f,
                                    1.0f / (1.0f + 0.05f * (std::sqrt (drivePre) - 1.0f)));

        // Tone-tilt shelf pair, pre-shaper. Positive tone = brighter.
        const float tiltDb = 9.0f * tone;
        for (int ch = 0; ch < 2; ++ch)
        {
            lowShelf[(size_t) ch].setLowShelf   (sr, 250.0,  -tiltDb);
            highShelf[(size_t) ch].setHighShelf (sr, 3500.0,  tiltDb);
        }

        auto* left  = buffer.getWritePointer (0);
        auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

        // Pre-shaper: stash dry (for the parallel mix) then tone-tilt in place.
        for (int i = 0; i < numSamples; ++i)
        {
            const float dryL = left[i];
            const float dryR = right != nullptr ? right[i] : left[i];
            dryStash.setSample (0, i, dryL);
            if (osChannels > 1) dryStash.setSample (1, i, dryR);

            float l = lowShelf[0].process (dryL);
            l = highShelf[0].process (l);
            left[i] = l;
            if (right != nullptr)
            {
                float r = lowShelf[1].process (dryR);
                r = highShelf[1].process (r);
                right[i] = r;
            }
        }

        // Shaper, 4x oversampled (only the nonlinearity runs hot).
        const int mdl = model;
        const float dp = drivePre, dc = driveComp, bs = bias;
        auto applyShaper = [mdl, dp, dc, bs] (float* d, int count)
        {
            for (int i = 0; i < count; ++i)
                d[i] = shape (mdl, d[i] * dp, bs) * dc;
        };

        if (driveOS != nullptr && numChannels == osChannels)
        {
            juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(),
                                                (size_t) numChannels, (size_t) numSamples);
            auto up = driveOS->processSamplesUp (block);
            for (int c = 0; c < numChannels; ++c)
                applyShaper (up.getChannelPointer ((size_t) c), (int) up.getNumSamples());
            driveOS->processSamplesDown (block);
        }
        else
        {
            applyShaper (left, numSamples);
            if (right != nullptr) applyShaper (right, numSamples);
        }

        // Post: DC-block -> parallel mix (dry delayed to match the oversampler)
        // -> output -> soft ceiling.
        int rp = dryRingPos;
        float blockPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float l = dcBlock[0].process (left[i]);
            float r = right != nullptr ? dcBlock[1].process (right[i]) : l;

            const float ddL = dryRing.getSample (0, rp);
            const float ddR = osChannels > 1 ? dryRing.getSample (1, rp) : ddL;
            dryRing.setSample (0, rp, dryStash.getSample (0, i));
            if (osChannels > 1) dryRing.setSample (1, rp, dryStash.getSample (1, i));
            if (++rp >= dryRingLen) rp = 0;

            l = ddL * (1.0f - mix) + l * mix;
            r = ddR * (1.0f - mix) + r * mix;

            l *= outGain; r *= outGain;

            l = softCeiling (l);
            r = softCeiling (r);

            left[i] = l;
            if (right != nullptr) right[i] = r;

            blockPeak = juce::jmax (blockPeak, std::abs (l), std::abs (r));
        }
        dryRingPos = rp;

        driveMeter.store (juce::jlimit (0.0f, 1.0f, blockPeak * mix), std::memory_order_relaxed);
    }

    float currentDriveMeter() const noexcept { return driveMeter.load (std::memory_order_relaxed); }

private:
    // Transparent soft ceiling: passed through UNCHANGED below the knee, so normal
    // levels are bit-exact; only the excursion above is tanh-compressed into the
    // remaining headroom, so the output asymptotes to +-1 and can never blow up.
    static constexpr float kKnee = 0.90f;
    static float softCeiling (float x) noexcept
    {
        const float ax = std::abs (x);
        if (ax <= kKnee) return x;
        const float over = ax - kKnee;
        const float room = 1.0f - kKnee;
        return std::copysign (kKnee + room * std::tanh (over / room), x);
    }

    // The per-model nonlinearity. `pre` is the driven input, `bias` the asymmetry
    // (-1..1). The offset is added before the curve and normalised out after, so the
    // rest position stays ~0 and bias colours rather than dominates.
    static float shape (int model, float pre, float bias) noexcept
    {
        const float off = 0.6f * bias;

        switch (model)
        {
            case 0: // Tube — smooth asymmetric tanh, gentle even/odd mix.
                return std::tanh (pre + off) - std::tanh (off);

            case 1: // Tape — softer knee (arctan), rounds the top, compresses.
            {
                const float k = 1.5f;
                const float s = 2.0f / juce::MathConstants<float>::pi;
                return s * std::atan (k * (pre + off)) - s * std::atan (k * off);
            }
            case 2: // Transformer — cubic soft-clip, firmer bend, richer low orders.
            {
                auto softCubic = [] (float x)
                {
                    x = juce::jlimit (-1.5f, 1.5f, x);
                    return juce::jlimit (-1.0f, 1.0f, x - (x * x * x) / 3.0f * 0.9f);
                };
                return softCubic (pre + off) - softCubic (off);
            }
            default: // 3 Transistor — harder soft-clip, edgier.
            {
                auto clip = [] (float x)
                {
                    const float ax = std::abs (x);
                    return x / (1.0f + ax + 0.25f * ax * ax);
                };
                return clip (pre + off) - clip (off);
            }
        }
    }

    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;   // transposed direct form II
        void reset() noexcept { z1 = z2 = 0; }
        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void setLowShelf (double fs, double f, double gainDb) noexcept
        {
            const double A   = std::pow (10.0, gainDb / 40.0);
            const double w0  = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (10.0, fs * 0.49, f) / fs;
            const double cw = std::cos (w0), sw = std::sin (w0);
            const double alpha = sw / (2.0 * 0.707);
            const double t = 2.0 * std::sqrt (A) * alpha;
            const double a0 = (A + 1.0) + (A - 1.0) * cw + t;
            b0 = (float) ((A * ((A + 1.0) - (A - 1.0) * cw + t)) / a0);
            b1 = (float) ((2.0 * A * ((A - 1.0) - (A + 1.0) * cw)) / a0);
            b2 = (float) ((A * ((A + 1.0) - (A - 1.0) * cw - t)) / a0);
            a1 = (float) ((-2.0 * ((A - 1.0) + (A + 1.0) * cw)) / a0);
            a2 = (float) (((A + 1.0) + (A - 1.0) * cw - t) / a0);
        }
        void setHighShelf (double fs, double f, double gainDb) noexcept
        {
            const double A   = std::pow (10.0, gainDb / 40.0);
            const double w0  = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (10.0, fs * 0.49, f) / fs;
            const double cw = std::cos (w0), sw = std::sin (w0);
            const double alpha = sw / (2.0 * 0.707);
            const double t = 2.0 * std::sqrt (A) * alpha;
            const double a0 = (A + 1.0) - (A - 1.0) * cw + t;
            b0 = (float) ((A * ((A + 1.0) + (A - 1.0) * cw + t)) / a0);
            b1 = (float) ((-2.0 * A * ((A - 1.0) + (A + 1.0) * cw)) / a0);
            b2 = (float) ((A * ((A + 1.0) + (A - 1.0) * cw - t)) / a0);
            a1 = (float) ((2.0 * ((A - 1.0) - (A + 1.0) * cw)) / a0);
            a2 = (float) (((A + 1.0) - (A - 1.0) * cw - t) / a0);
        }
    };

    // First-order DC blocker (removes the offset the bias asymmetry injects).
    struct DcBlocker
    {
        float x1 = 0.0f, y1 = 0.0f, r = 0.9975f;
        void reset() noexcept { x1 = y1 = 0.0f; }
        float process (float x) noexcept
        {
            const float y = x - x1 + r * y1;
            x1 = x; y1 = y;
            return y;
        }
    };

    float p (const char* id) const noexcept
    {
        return cachedParameterValue (id);
    }

    static float dbToGain (float db) noexcept { return juce::Decibels::decibelsToGain (db, -120.0f); }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    std::array<Biquad, 2> lowShelf, highShelf;
    std::array<DcBlocker, 2> dcBlock;

    std::unique_ptr<juce::dsp::Oversampling<float>> driveOS;
    int osChannels = 0;
    juce::AudioBuffer<float> dryStash;   // original input for this block
    juce::AudioBuffer<float> dryRing;    // latency-comp delay (length = OS latency)
    int dryRingLen = 1, dryRingPos = 0;

    std::atomic<float> driveMeter { 0.0f };
};

} // namespace sattari::dsp
