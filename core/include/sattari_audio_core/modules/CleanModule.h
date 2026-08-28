#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <cmath>

// =============================================================================
//  Clean — three-band adaptive downward expander (broadband noise reduction).
// -----------------------------------------------------------------------------
//  Splits into low/mid/high with Linkwitz-Riley crossovers and learns a noise
//  FLOOR per band: the floor settles down toward the smoothed band level over
//  ~40 ms so it sits on the steady noise rather than momentary troughs, and
//  creeps up only very slowly so programme peaks can't drag it along. Anything
//  under floor x Sensitivity is expanded downward by Amount.
//
//  Lifted verbatim from the standalone plugin's DSP, which now wraps this.
// =============================================================================

namespace sattari::dsp
{

class CleanModule final : public SattariModule
{
public:
    static constexpr int kBands = 3;

    const char* moduleId()    const noexcept override { return "clean"; }
    const char* displayName() const noexcept override { return "Clean"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using FP = juce::AudioParameterFloat;
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "amount", 1 }, "Amount",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "sensitivity", 1 }, "Sensitivity",
            juce::NormalisableRange<float> (1.5f, 12.0f, 0.1f), 4.0f));   // threshold = floor * this
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "output", 1 }, "Output",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        pAmount = apvts.getRawParameterValue (prefix + "amount");
        pSens   = apvts.getRawParameterValue (prefix + "sensitivity");
        pOut    = apvts.getRawParameterValue (prefix + "output");
    }

    void prepare (double sampleRate, int maxBlockSize, int /*numChannels*/) override
    {
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, maxBlockSize), 2 };
        lr1.prepare (spec); lr1.setType (juce::dsp::LinkwitzRileyFilterType::allpass); lr1.setCutoffFrequency (450.0f);
        lr2.prepare (spec); lr2.setType (juce::dsp::LinkwitzRileyFilterType::allpass); lr2.setCutoffFrequency (3000.0f);
        for (auto& e : env) e.prepare (sampleRate, 10.0f, 150.0f);   // steady band level
        reset();
    }

    void reset() override
    {
        lr1.reset(); lr2.reset();
        for (auto& e : env) e.reset (0.0f);
        floorLevel = { 1.0f, 1.0f, 1.0f };
        gSmooth    = { 1.0f, 1.0f, 1.0f };
        reductionDb.store (0.0f, std::memory_order_relaxed);
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0) return;

        const float amount  = val (pAmount, 0.5f);
        const float threshM = val (pSens, 4.0f);
        const float outGain = juce::Decibels::decibelsToGain (val (pOut, 0.0f));

        float maxRed = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float band[kBands][2] = { { 0,0 }, { 0,0 }, { 0,0 } };
            for (int ch = 0; ch < numCh; ++ch)
            {
                float low = 0.0f, rest = 0.0f, mid = 0.0f, high = 0.0f;
                lr1.processSample (ch, buffer.getReadPointer (ch)[i], low, rest);
                lr2.processSample (ch, rest, mid, high);
                band[0][ch] = low; band[1][ch] = mid; band[2][ch] = high;
            }

            float g[kBands];
            for (int b = 0; b < kBands; ++b)
            {
                const float peak = std::max (std::fabs (band[b][0]), std::fabs (band[b][1]));
                const float e = env[(size_t) b].processRectified (peak);

                // Adaptive noise floor: settle DOWN toward the (already smoothed) band
                // level over ~40 ms so it sits on the steady noise, not the momentary
                // troughs; creep UP slowly so signal peaks don't drag it along.
                if (e < floorLevel[(size_t) b]) floorLevel[(size_t) b] += 0.0006f * (e - floorLevel[(size_t) b]);
                else floorLevel[(size_t) b] *= 1.00001f;
                const float fl = std::max (floorLevel[(size_t) b], 1.0e-6f);

                const float thresh = fl * threshM;
                float target = 1.0f;
                if (e < thresh) target = (1.0f - amount) + amount * (e / thresh);   // downward expand
                gSmooth[(size_t) b] += 0.02f * (target - gSmooth[(size_t) b]);       // de-zipper
                g[b] = gSmooth[(size_t) b];
                maxRed = std::max (maxRed, 1.0f - g[b]);
            }

            for (int ch = 0; ch < numCh; ++ch)
            {
                float y = 0.0f;
                for (int b = 0; b < kBands; ++b) y += band[b][ch] * g[b];
                buffer.getWritePointer (ch)[i] = y * outGain;
            }
        }

        reductionDb.store (juce::Decibels::gainToDecibels (1.0f - juce::jlimit (0.0f, 0.999f, maxRed)),
                           std::memory_order_relaxed);
    }

    // Reduction readout for meters (audio -> UI, lock-free).
    float getReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }

private:
    static float val (std::atomic<float>* p, float fallback) noexcept
    { return p != nullptr ? p->load() : fallback; }

    std::atomic<float>* pAmount = nullptr;
    std::atomic<float>* pSens = nullptr;
    std::atomic<float>* pOut = nullptr;

    juce::dsp::LinkwitzRileyFilter<float> lr1, lr2;             // crossovers (low|rest, mid|high)
    std::array<sattari::dsp::EnvelopeFollower, kBands> env;      // stereo-linked, per band
    std::array<float, kBands> floorLevel { { 1.0f, 1.0f, 1.0f } };  // min-follower -> noise floor
    std::array<float, kBands> gSmooth    { { 1.0f, 1.0f, 1.0f } };
    std::atomic<float> reductionDb { 0.0f };
};

} // namespace sattari::dsp
