// Stem-scoped FX: the phase-alignment contract.
//
// The four stems are split from ONE source and summed straight back together. If one
// stem's plugin chain reports latency and the others don't, the sum comb-filters — a
// far worse failure than a plain delay, and one that is inaudible on a sine and
// obvious on a mix. This test pins the compensation.
//
// It drives TrackDeckBody's stem compensation directly with a synthetic "plugin" that
// delays by a known amount, and asserts the summed output stays phase-coherent.

#include "TrackDeckBody.h"
#include <iostream>
#include <cmath>
#include <vector>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (! ok) ++failures;
}

// A stand-in for a hosted plugin that reports `latency` samples of delay.
struct DelayPlugin
{
    int latency = 0;
    std::vector<std::vector<float>> line;
    int w = 0;

    void prepare (int channels, int cap)
    {
        line.assign ((size_t) channels, std::vector<float> ((size_t) juce::jmax (1, cap), 0.0f));
        w = 0;
    }

    void process (juce::AudioBuffer<float>& b)
    {
        if (latency <= 0) return;
        const int cap = (int) line[0].size();
        int wi = w;
        for (int c = 0; c < b.getNumChannels() && c < (int) line.size(); ++c)
        {
            auto* io = b.getWritePointer (c);
            auto& dl = line[(size_t) c];
            wi = w;
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const int ri = (wi - latency + cap) % cap;
                const float out = dl[(size_t) ri];
                dl[(size_t) wi] = io[i];
                io[i] = out;
                wi = (wi + 1) % cap;
            }
        }
        w = wi;
    }
};
} // namespace

int main()
{
    std::cout << "StemFx contract\n";

    constexpr double sr = 48000.0;
    constexpr int block = 512, channels = 2;

    // ── 1. Compensation aligns stems with unequal chain latency ──────────────
    {
        TrackDeckBody body;
        body.prepare (sr, block, channels);

        // Only the vocals stem (index 3) hosts a plugin, with 128 samples of latency.
        constexpr int kLatency = 128;
        body.setStemChainLatency (3, kLatency);
        check (body.getMaxStemChainLatency() == kLatency, "max stem latency reports the slowest stem");

        DelayPlugin vox; vox.latency = kLatency; vox.prepare (channels, 4096);

        // Feed each stem an identical impulse train; after compensation every stem must
        // present the SAME sample at the same index, so the sum is coherent.
        std::array<juce::AudioBuffer<float>, 4> stems;
        for (auto& s : stems) { s.setSize (channels, block); }

        int impulseAt = -1;
        std::array<int, 4> peakIndex { -1, -1, -1, -1 };

        for (int blk = 0; blk < 8; ++blk)
        {
            for (int s = 0; s < 4; ++s)
            {
                stems[(size_t) s].clear();
                // One impulse in block 1, sample 40, on every stem.
                if (blk == 1)
                    for (int c = 0; c < channels; ++c)
                        stems[(size_t) s].setSample (c, 40, 1.0f);
            }
            if (blk == 1) impulseAt = 1 * block + 40;

            // The vocals stem goes through its "plugin".
            vox.process (stems[3]);

            juce::AudioBuffer<float>* ptrs[4] = { &stems[0], &stems[1], &stems[2], &stems[3] };
            body.testApplyStemCompensation (ptrs, channels, block);

            for (int s = 0; s < 4; ++s)
                for (int i = 0; i < block; ++i)
                    if (peakIndex[(size_t) s] < 0 && stems[(size_t) s].getSample (0, i) > 0.5f)
                        peakIndex[(size_t) s] = blk * block + i;
        }

        check (impulseAt > 0, "impulse was injected");
        const bool allFound = peakIndex[0] >= 0 && peakIndex[1] >= 0 && peakIndex[2] >= 0 && peakIndex[3] >= 0;
        check (allFound, "every stem produced its impulse");
        if (allFound)
        {
            const bool aligned = peakIndex[0] == peakIndex[1]
                              && peakIndex[1] == peakIndex[2]
                              && peakIndex[2] == peakIndex[3];
            check (aligned, "all four stems land on the SAME sample after compensation"
                            " (got " + juce::String (peakIndex[0]) + "/" + juce::String (peakIndex[1])
                          + "/" + juce::String (peakIndex[2]) + "/" + juce::String (peakIndex[3]) + ")");
            check (peakIndex[0] == impulseAt + kLatency,
                   "the whole deck is delayed by exactly the slowest stem");
        }
    }

    // ── 2. No latency anywhere == no delay and no work ───────────────────────
    {
        TrackDeckBody body;
        body.prepare (sr, block, channels);
        check (body.getMaxStemChainLatency() == 0, "no stem chains -> zero latency");

        std::array<juce::AudioBuffer<float>, 4> stems;
        for (auto& s : stems) { s.setSize (channels, block); s.clear(); s.setSample (0, 7, 1.0f); }
        juce::AudioBuffer<float>* ptrs[4] = { &stems[0], &stems[1], &stems[2], &stems[3] };
        body.testApplyStemCompensation (ptrs, channels, block);
        bool untouched = true;
        for (auto& s : stems) untouched = untouched && s.getSample (0, 7) > 0.99f;
        check (untouched, "zero-latency path passes stems through untouched");
    }

    std::cout << (failures == 0 ? "STEMFX_OK\n" : "STEMFX_FAILED\n");
    return failures == 0 ? 0 : 1;
}
