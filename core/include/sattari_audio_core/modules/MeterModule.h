#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariKWeighting.h>

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// =============================================================================
//  Meter — LUFS (momentary / short-term / integrated), true peak, correlation.
// -----------------------------------------------------------------------------
//  ITU-R BS.1770 loudness. The K-weighted, channel-summed square (z^2) is pushed
//  through a 3 s ring; momentary (0.4 s) and short-term (3 s) are sliding sums over
//  it, and integrated is an absolute-gated running mean (gate at -70 LUFS).
//  True peak is 4x linear interpolation with a slow decay so the reading falls back.
//
//  Measures only — audio passes through untouched, which is what makes it safe to
//  drop anywhere in a rack.
//
//  The K-weighting filters and inter-sample peak come from SattariKWeighting.h,
//  shared with Peak, rather than being transcribed a second time.
// =============================================================================

namespace sattari::dsp
{

class MeterModule final : public SattariModule
{
public:
    const char* moduleId()    const noexcept override { return "meter"; }
    const char* displayName() const noexcept override { return "Meter"; }

    struct Readout
    {
        float momentaryLufs = -100.0f, shortLufs = -100.0f, integratedLufs = -100.0f;
        float truePeakDb = -100.0f, correlation = 0.0f;
    };

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        // Target loudness reference line for the display only (does not alter audio).
        out.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { prefix + "target", 1 }, "Target LUFS",
            juce::NormalisableRange<float> (-36.0f, -6.0f, 0.1f), -14.0f));
    }

    void link (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        pTarget = apvts.getRawParameterValue (prefix + "target");
    }

    void prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override
    {
        sr_ = sampleRate;
        for (auto& b : shelf) designKWeightingShelf (b, sampleRate);
        for (auto& b : hp)    designKWeightingHighPass (b, sampleRate);
        shortLen = (int) std::round (sampleRate * 3.0);
        momLen   = (int) std::round (sampleRate * 0.4);
        zCap = juce::jmax (1, shortLen);
        zRing.assign ((size_t) zCap, 0.0f);   // allocation belongs here, never in process()
        reset();
    }

    void reset() override
    {
        for (auto& b : shelf) b.reset();
        for (auto& b : hp)    b.reset();
        std::fill (zRing.begin(), zRing.end(), 0.0f);
        zWrite = 0; momSum = shortSum = 0.0;
        intSum = 0.0; intCount = 0;
        tpHist = { 0.0f, 0.0f }; tpMax = 0.0f;
        corrLL = corrRR = corrLR = 0.0; corrCount = 0;
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0 || zCap <= 0) return;

        if (intResetFlag.exchange (false)) { intSum = 0.0; intCount = 0; }

        for (int i = 0; i < n; ++i)
        {
            double zsum = 0.0;
            float lch = 0.0f, rch = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float x = buffer.getReadPointer (ch)[i];
                const double kw = hp[(size_t) ch].process (shelf[(size_t) ch].process ((double) x));
                zsum += kw * kw;

                tpMax = std::max (tpMax, interSamplePeak (tpHist[(size_t) ch], x));
                tpHist[(size_t) ch] = x;
                if (ch == 0) lch = x; else rch = x;
            }
            if (numCh == 1) rch = lch;

            // Sliding sums for momentary (0.4 s) and short-term (3 s).
            const float leavingShort = zRing[(size_t) zWrite];
            const int momIdx = (zWrite - momLen + zCap) % zCap;
            const float leavingMom = zRing[(size_t) momIdx];
            shortSum += zsum - leavingShort;
            momSum   += zsum - leavingMom;
            zRing[(size_t) zWrite] = (float) zsum;
            if (++zWrite >= zCap) zWrite = 0;

            // Integrated: absolute-gated running mean of z^2 (gate at -70 LUFS).
            if (zsum > 0.0)
            {
                const double instLoud = -0.691 + 10.0 * std::log10 (zsum);
                if (instLoud > -70.0) { intSum += zsum; ++intCount; }
            }

            corrLL += (double) lch * lch; corrRR += (double) rch * rch; corrLR += (double) lch * rch;
            if (++corrCount >= (int) (sr_ * 0.3))
            {
                const double den = std::sqrt (corrLL * corrRR);
                corr.store (den > 1.0e-12 ? (float) juce::jlimit (-1.0, 1.0, corrLR / den) : 0.0f,
                            std::memory_order_relaxed);
                corrLL = corrRR = corrLR = 0.0; corrCount = 0;
            }
        }

        auto lufs = [] (double meanSq) { return meanSq > 0.0 ? (float) (-0.691 + 10.0 * std::log10 (meanSq)) : -100.0f; };
        momLufs.store    (lufs (momSum   / juce::jmax (1, momLen)),   std::memory_order_relaxed);
        shortLufs_.store (lufs (shortSum / juce::jmax (1, shortLen)), std::memory_order_relaxed);
        intLufs.store    (intCount > 0 ? lufs (intSum / (double) intCount) : -100.0f, std::memory_order_relaxed);
        tpDb.store       (tpMax > 0.0f ? juce::Decibels::gainToDecibels (tpMax) : -100.0f, std::memory_order_relaxed);
        tpMax *= 0.90f;   // slow decay so the reading falls back
    }

    Readout getReadout() const noexcept
    {
        Readout r;
        r.momentaryLufs  = momLufs.load (std::memory_order_relaxed);
        r.shortLufs      = shortLufs_.load (std::memory_order_relaxed);
        r.integratedLufs = intLufs.load (std::memory_order_relaxed);
        r.truePeakDb     = tpDb.load (std::memory_order_relaxed);
        r.correlation    = corr.load (std::memory_order_relaxed);
        return r;
    }

    void resetIntegrated() { intResetFlag.store (true, std::memory_order_relaxed); }

    float targetLufs() const noexcept { return pTarget != nullptr ? pTarget->load() : -14.0f; }

private:
    std::atomic<float>* pTarget = nullptr;
    double sr_ = 44100.0;

    std::array<KBiquad, 2> shelf, hp;   // K-weighting, per channel

    // Sliding-window sums of the K-weighted mono-summed square (z^2).
    std::vector<float> zRing;           // 3 s of z^2
    int   zCap = 0, zWrite = 0;
    double momSum = 0.0, shortSum = 0.0;
    int   momLen = 0, shortLen = 0;     // window lengths in samples

    // Integrated (absolute-gated running mean of z^2).
    double intSum = 0.0; long long intCount = 0;
    std::atomic<bool> intResetFlag { false };

    // True-peak (4x oversample) with slow decay + correlation accumulators.
    std::array<float, 2> tpHist { { 0.0f, 0.0f } };
    float tpMax = 0.0f;
    double corrLL = 0.0, corrRR = 0.0, corrLR = 0.0; int corrCount = 0;

    std::atomic<float> momLufs { -100.0f }, shortLufs_ { -100.0f }, intLufs { -100.0f },
                       tpDb { -100.0f }, corr { 0.0f };
};

} // namespace sattari::dsp
