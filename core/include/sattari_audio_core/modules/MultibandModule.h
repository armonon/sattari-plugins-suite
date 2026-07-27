#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>

#include <array>
#include <atomic>
#include <cmath>

// =============================================================================
//  MultibandModule — Sattari Multiband's DSP, extracted so the standalone plugin
//  and a Mix rack slot run the SAME 3-band compressor.
//
//  Two Linkwitz-Riley crossovers in ALLPASS mode: lr1 splits (low | rest), lr2
//  splits rest into (mid | high). Allpass is the point — the three bands sum back
//  to the input phase-coherently and flat, which is what makes a recombining
//  multiband transparent at unity.
//
//  Each band gets the same log-domain soft-knee gain computer and asymmetric
//  envelope smoothing as CompModule, plus makeup and solo; the bands are then
//  summed, blended against dry, trimmed and soft-ceilinged.
// =============================================================================

namespace sattari::dsp
{

class MultibandModule final : public SattariModule
{
public:
    static constexpr int kBands = 3;

    const char* moduleId()    const noexcept override { return "multiband"; }
    const char* displayName() const noexcept override { return "Multiband"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        auto f = [&out, &prefix] (const juce::String& id, const juce::String& name,
                                  float lo, float hi, float def, float step, float skew,
                                  const char* suffix)
        {
            juce::NormalisableRange<float> r (lo, hi, step);
            if (skew != 1.0f) r.setSkewForCentre (lo + (hi - lo) * skew);
            out.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID (prefix + id, 1), name, r, def,
                juce::AudioParameterFloatAttributes().withLabel (suffix)));
        };

        f ("xLow",  "Low/Mid X",  40.0f,  800.0f,   250.0f,  1.0f, 0.35f, "Hz");
        f ("xHigh", "Mid/High X", 800.0f, 12000.0f, 3000.0f, 1.0f, 0.35f, "Hz");

        const char* nms[kBands]      = { "Low", "Mid", "High" };
        const float defThr[kBands]   = { -18.0f, -20.0f, -22.0f };
        const float defRatio[kBands] = {   2.5f,   3.0f,   2.5f };
        const float defAtk[kBands]   = {  15.0f,  10.0f,   4.0f };
        const float defRel[kBands]   = { 160.0f, 120.0f,  90.0f };

        for (int b = 0; b < kBands; ++b)
        {
            const juce::String n = juce::String (b + 1);
            const juce::String nm (nms[b]);
            f ("thr"   + n, nm + " Threshold", -60.0f,  0.0f,   defThr[b],   0.1f,  1.0f, "dB");
            f ("ratio" + n, nm + " Ratio",       1.0f, 20.0f,   defRatio[b], 0.01f, 0.5f, ":1");
            f ("knee"  + n, nm + " Knee",        0.0f, 24.0f,   6.0f,        0.1f,  1.0f, "dB");
            f ("atk"   + n, nm + " Attack",      0.1f, 120.0f,  defAtk[b],   0.1f,  0.5f, "ms");
            f ("rel"   + n, nm + " Release",    20.0f, 1500.0f, defRel[b],   1.0f,  0.5f, "ms");
            f ("mk"    + n, nm + " Makeup",    -12.0f, 24.0f,   0.0f,        0.1f,  1.0f, "dB");
            out.push_back (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID (prefix + "solo" + n, 1), nm + " Solo", false));
        }

        f ("mix",    "Mix",      0.0f, 100.0f, 100.0f, 0.1f, 1.0f, "%");
        f ("output", "Output", -24.0f,  24.0f,   0.0f, 0.1f, 1.0f, "dB");
    }

    void link (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int maxBlockSize, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, maxBlockSize), 2 };
        lr1.prepare (spec); lr1.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
        lr2.prepare (spec); lr2.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
        for (auto& g : grSmoother) g.prepare (sr, 10.0f, 120.0f);
        reset();
    }

    void reset() override
    {
        lr1.reset(); lr2.reset();
        for (auto& g : grSmoother) g.reset (0.0f);
        smoothedGrDb = { { 0.0f, 0.0f, 0.0f } };
        for (auto& a : bandGrDb) a.store (0.0f, std::memory_order_relaxed);
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (state == nullptr) return;
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh == 0 || n == 0) return;

        // Clamp mid/high above low so the split stays valid.
        const float xLow  = juce::jlimit (20.0f, (float) (sr * 0.45), p ("xLow"));
        const float xHigh = juce::jlimit (xLow * 1.1f, (float) (sr * 0.45), p ("xHigh"));
        lr1.setCutoffFrequency (xLow);
        lr2.setCutoffFrequency (xHigh);

        float thr[kBands], invRatio[kBands], halfKnee[kBands], kneeW[kBands], makeup[kBands];
        bool  anySolo = false;
        bool  solo[kBands];
        for (int b = 0; b < kBands; ++b)
        {
            const juce::String n1 = juce::String (b + 1);
            thr[b]      = p ("thr"   + n1);
            const float ratio = juce::jmax (1.0f, p ("ratio" + n1));
            invRatio[b] = 1.0f / ratio;
            kneeW[b]    = juce::jmax (0.0f, p ("knee" + n1));
            halfKnee[b] = 0.5f * kneeW[b];
            grSmoother[(size_t) b].setTimes (p ("atk" + n1), p ("rel" + n1));
            makeup[b]   = dbToGain (p ("mk" + n1));
            solo[b]     = p ("solo" + n1) > 0.5f;
            anySolo     = anySolo || solo[b];
        }

        const float mix     = juce::jlimit (0.0f, 1.0f, p ("mix") * 0.01f);
        const float outGain = dbToGain (p ("output"));

        float grForMeter[kBands] = { 0.0f, 0.0f, 0.0f };

        for (int i = 0; i < n; ++i)
        {
            // Split into 3 bands per channel (recombines flat at unity gain).
            float band[kBands][2] = { { 0,0 }, { 0,0 }, { 0,0 } };
            for (int ch = 0; ch < numCh; ++ch)
            {
                float low = 0.0f, rest = 0.0f, mid = 0.0f, high = 0.0f;
                lr1.processSample (ch, buffer.getReadPointer (ch)[i], low, rest);
                lr2.processSample (ch, rest, mid, high);
                band[0][ch] = low; band[1][ch] = mid; band[2][ch] = high;
            }

            float bandGain[kBands];
            for (int b = 0; b < kBands; ++b)
            {
                float det = 0.0f;
                for (int ch = 0; ch < numCh; ++ch)
                    det = std::max (det, std::fabs (band[b][ch]));

                const float detDb = juce::Decibels::gainToDecibels (det + 1.0e-9f, -120.0f);

                const float over = detDb - thr[b];
                float targetGrDb;
                if (kneeW[b] > 0.0001f && over > -halfKnee[b] && over < halfKnee[b])
                {
                    const float x = over + halfKnee[b];                  // 0 .. kneeW
                    targetGrDb = -((1.0f - invRatio[b]) * (x * x) / (2.0f * kneeW[b]));
                }
                else if (over <= -halfKnee[b])
                {
                    targetGrDb = 0.0f;                                   // below knee
                }
                else
                {
                    targetGrDb = -(over) * (1.0f - invRatio[b]);         // hard-knee region
                }

                smoothedGrDb[(size_t) b] = grSmoother[(size_t) b].processFalling (targetGrDb);
                bandGain[b] = dbToGain (smoothedGrDb[(size_t) b]);
                grForMeter[b] = std::max (grForMeter[b], -smoothedGrDb[(size_t) b]);
            }

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float dry = buffer.getReadPointer (ch)[i];
                float wet = 0.0f;
                for (int b = 0; b < kBands; ++b)
                {
                    if (anySolo && ! solo[b]) continue;   // solo mutes the rest
                    wet += band[b][ch] * bandGain[b] * makeup[b];
                }

                float outv = dry * (1.0f - mix) + wet * mix;
                outv *= outGain;

                // Transparent soft ceiling: tanh only bites near the rails.
                if (outv > 0.8f || outv < -0.8f)
                    outv = std::tanh (outv);
                buffer.getWritePointer (ch)[i] = juce::jlimit (-1.5f, 1.5f, outv);
            }
        }

        for (int b = 0; b < kBands; ++b)
            bandGrDb[(size_t) b].store (grForMeter[b], std::memory_order_relaxed);
    }

    float currentBandGrDb (int band) const noexcept
    {
        return juce::isPositiveAndBelow (band, kBands)
                 ? bandGrDb[(size_t) band].load (std::memory_order_relaxed) : 0.0f;
    }

private:
    float p (const juce::String& id) const noexcept
    {
        auto* v = state->getRawParameterValue (pfx + id);
        return v != nullptr ? v->load() : 0.0f;
    }

    static float dbToGain (float db) noexcept { return juce::Decibels::decibelsToGain (db, -120.0f); }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    juce::dsp::LinkwitzRileyFilter<float> lr1, lr2;
    std::array<EnvelopeFollower, kBands> grSmoother;
    std::array<float, kBands> smoothedGrDb { { 0.0f, 0.0f, 0.0f } };
    std::array<std::atomic<float>, kBands> bandGrDb {};
};

} // namespace sattari::dsp
