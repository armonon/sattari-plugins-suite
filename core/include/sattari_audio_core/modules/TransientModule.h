#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>

#include <atomic>
#include <cmath>

// =============================================================================
//  Transient — attack / sustain shaper.
// -----------------------------------------------------------------------------
//  Differential envelope design: two attack detectors that differ only in attack
//  speed, and two sustain detectors that differ only in release speed. The gap
//  between each pair IS the envelope — fast-minus-slow is the onset spike,
//  slow-minus-fast is the tail — normalised by a stable level estimate so the
//  shaping is level-independent.
//
//  Lifted verbatim from the standalone plugin's DSP, which now wraps this.
// =============================================================================

namespace sattari::dsp
{

class TransientModule final : public SattariModule
{
public:
    const char* moduleId()    const noexcept override { return "transient"; }
    const char* displayName() const noexcept override { return "Transient"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using P = juce::AudioParameterFloat;
        out.push_back (std::make_unique<P> (juce::ParameterID { prefix + "attack", 1 }, "Attack",
            juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
        out.push_back (std::make_unique<P> (juce::ParameterID { prefix + "sustain", 1 }, "Sustain",
            juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
        out.push_back (std::make_unique<P> (juce::ParameterID { prefix + "output", 1 }, "Output",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    }

    void link (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        pAttack  = apvts.getRawParameterValue (prefix + "attack");
        pSustain = apvts.getRawParameterValue (prefix + "sustain");
        pOutput  = apvts.getRawParameterValue (prefix + "output");
    }

    void prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override
    {
        // Attack pair: same (medium) release, different attack speeds.
        atkFast.prepare (sampleRate, 0.2f, 120.0f);
        atkSlow.prepare (sampleRate, 18.0f, 120.0f);
        // Sustain pair: same (medium) attack, different release speeds.
        susFast.prepare (sampleRate, 8.0f, 40.0f);
        susSlow.prepare (sampleRate, 8.0f, 320.0f);
        reset();
    }

    void reset() override
    {
        for (auto* e : { &atkFast, &atkSlow, &susFast, &susSlow }) e->reset (0.0f);
        gainDb.store (0.0f, std::memory_order_relaxed);
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0) return;

        const float attack  = pAttack  != nullptr ? pAttack->load()  : 0.0f;
        const float sustain = pSustain != nullptr ? pSustain->load() : 0.0f;
        const float outGain = juce::Decibels::decibelsToGain (pOutput != nullptr ? pOutput->load() : 0.0f);

        float lastGain = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            float xm = 0.0f;
            for (int ch = 0; ch < numCh; ++ch) xm = std::max (xm, std::fabs (buffer.getReadPointer (ch)[i]));

            const float aF = atkFast.processRectified (xm);
            const float aS = atkSlow.processRectified (xm);
            const float sF = susFast.processRectified (xm);
            const float sS = susSlow.processRectified (xm);

            const float ref = aS + 1.0e-4f;                                    // stable level estimate
            const float attackEnv  = juce::jlimit (0.0f, 1.0f, (aF - aS) / ref);  // onset spike
            const float sustainEnv = juce::jlimit (0.0f, 1.0f, (sS - sF) / ref);  // tail

            float g = 1.0f + 2.0f * attack * attackEnv + 1.5f * sustain * sustainEnv;
            g = juce::jlimit (0.05f, 4.0f, g);
            lastGain = g;

            for (int ch = 0; ch < numCh; ++ch)
                buffer.getWritePointer (ch)[i] = buffer.getReadPointer (ch)[i] * g * outGain;
        }

        gainDb.store (juce::Decibels::gainToDecibels (lastGain), std::memory_order_relaxed);
    }

    // Gain-reduction readout for meters (audio -> UI, lock-free).
    float getGainDb() const noexcept { return gainDb.load (std::memory_order_relaxed); }

private:
    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pSustain = nullptr;
    std::atomic<float>* pOutput = nullptr;

    sattari::dsp::EnvelopeFollower atkFast, atkSlow;   // difference = attack env
    sattari::dsp::EnvelopeFollower susFast, susSlow;   // difference = sustain env
    std::atomic<float> gainDb { 0.0f };
};

} // namespace sattari::dsp
