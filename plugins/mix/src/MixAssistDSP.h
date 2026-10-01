#pragma once

#include "MixPlanner.h"

namespace sattari::automix
{
class AssistDSP
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        compressor.prepare ({ sr, (juce::uint32) juce::jmax (1, blockSize), 1 });
        compressor.setAttack (20.0f); compressor.setRelease (180.0f);
        gain.reset (sr, 0.05); wet.reset (sr, 0.05);
        reset();
    }
    void reset()
    {
        for (auto& f : low) f.reset();
        for (auto& f : presence) f.reset();
        compressor.reset();
        gain.setCurrentAndTargetValue (1.0f); wet.setCurrentAndTargetValue (0.0f);
        lastLow = lastPresence = 0;
    }
    void prime (const Settings& s)
    {
        gain.setCurrentAndTargetValue ((float) amplitude (effectiveGain (s)));
        wet.setCurrentAndTargetValue (s.enabled ? s.amount : 0.0f);
        lastLow = s.low; lastPresence = s.presence;
    }
    void process (juce::AudioBuffer<float>& audio, const Settings& s) noexcept
    {
        gain.setTargetValue ((float) amplitude (effectiveGain (s)));
        wet.setTargetValue (s.enabled ? s.amount : 0.0f);
        if (! wet.isSmoothing() && wet.getCurrentValue() == 0.0f)
        {
            for (auto& f : low) f.reset();
            for (auto& f : presence) f.reset();
            compressor.reset();
            if (! gain.isSmoothing() && gain.getCurrentValue() == 1.0f) return;
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                const auto g = gain.getNextValue();
                for (int c = 0; c < audio.getNumChannels(); ++c) audio.getWritePointer (c)[i] *= g;
            }
            return;
        }
        compressor.setThreshold (s.threshold);
        compressor.setRatio (1.0f + 1.5f * s.compression);
        const int channels = juce::jmin (2, audio.getNumChannels());
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            if ((i & 31) == 0)
            {
                const float a = (float) (1.0 - std::exp (-32.0 / (sr * 0.025)));
                lastLow += a * (s.low - lastLow); lastPresence += a * (s.presence - lastPresence);
                setBell (low, 120.0f, lastLow); setBell (presence, 2200.0f, lastPresence);
            }
            float detector = 0;
            for (int c = 0; c < channels; ++c) detector = std::max (detector, std::abs (audio.getSample (c, i)));
            const float compressed = compressor.processSample (0, detector);
            const float reduction = detector > 1.0e-9f ? compressed / detector : 1.0f;
            const float w = wet.getNextValue(), g = gain.getNextValue();
            for (int c = 0; c < channels; ++c)
            {
                const float x = audio.getSample (c, i);
                const float filtered = (float) presence[(size_t) c].process (low[(size_t) c].process (x));
                // Exact dry path at the default state. Shared detector preserves stereo image.
                audio.setSample (c, i, (x + w * (filtered * reduction - x)) * g);
            }
        }
    }
private:
    void setBell (std::array<dsp::KBiquad, 2>& filters, float hz, float db) noexcept
    {
        const auto c = juce::dsp::IIR::ArrayCoefficients<double>::makePeakFilter (
            sr, juce::jmin ((double) hz, sr * 0.4), 0.8, amplitude (db));
        for (auto& f : filters)
        { f.b0 = c[0] / c[3]; f.b1 = c[1] / c[3]; f.b2 = c[2] / c[3]; f.a1 = c[4] / c[3]; f.a2 = c[5] / c[3]; }
    }
    double sr = 48000;
    float lastLow = 0, lastPresence = 0;
    std::array<dsp::KBiquad, 2> low, presence;
    juce::dsp::Compressor<float> compressor;
    juce::SmoothedValue<float> gain, wet;
};
} // namespace sattari::automix
