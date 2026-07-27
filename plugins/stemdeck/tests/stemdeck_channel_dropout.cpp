// Zero-dropout add/remove-under-playback contract (the spec's #1 live mandate and
// the channel-architecture release gate). A real concurrency test, not a synthetic
// per-block one:
//
//   * a "message thread" hammers ChannelGraph::mutate() — adding, removing, and
//     reordering silent churn channels as fast as it can;
//   * an "audio thread" renders the graph block-by-block into a master buffer.
//
// One PERSISTENT channel emits a phase-continuous sine derived from the global
// sample index; the churn channels are SILENT. So the master must ALWAYS equal that
// exact sine — any torn read of a half-built snapshot, use-after-free of a removed
// channel, or dropped block would deviate. The audio thread checks every sample
// against the expected sine and flags any discontinuity.
//
//   StemDeckChannelDropout → prints CHANNEL_DROPOUT_OK on success
#include "Channel.h"
#include "GraphSnapshot.h"
#include <atomic>
#include <thread>
#include <cmath>
#include <iostream>

namespace
{
constexpr double sr = 44100.0;
constexpr double freq = 220.0;

inline float expectedSample (juce::int64 idx)
{
    return 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * freq * (double) idx / sr);
}

// Phase-continuous sine keyed off the global sample index carried in BlockContext.
class SineBody : public ChannelBody
{
public:
    ChannelType type() const noexcept override { return ChannelType::trackDeck; }
    void prepare (double, int, int) override {}
    void processInto (juce::AudioBuffer<float>& scratch, const BlockContext& ctx) noexcept override
    {
        for (int i = 0; i < ctx.numSamples; ++i)
        {
            const float v = expectedSample (ctx.blockStartSample + i);
            for (int ch = 0; ch < scratch.getNumChannels(); ++ch)
                scratch.setSample (ch, i, v);
        }
    }
};

class SilentBody : public ChannelBody
{
public:
    ChannelType type() const noexcept override { return ChannelType::trackDeck; }
    void prepare (double, int, int) override {}
    void processInto (juce::AudioBuffer<float>&, const BlockContext&) noexcept override {}
};
}

int main()
{
    constexpr int block = 256, chans = 2;
    constexpr juce::int64 totalBlocks = 60000;   // ~5.8 min of audio at 44.1k/256

    ChannelGraph graph;
    std::atomic<juce::int64> nextId { 1 };

    auto makeSilent = [&]
    {
        auto c = std::make_shared<Channel> (nextId.fetch_add (1), std::make_unique<SilentBody>());
        c->cfSide = CrossfaderSide::center;
        c->prepare (sr, block, chans);
        return c;
    };

    // The persistent channel — center side (bus gain 1.0), never removed.
    {
        auto persistent = std::make_shared<Channel> (0, std::make_unique<SineBody>());
        persistent->cfSide = CrossfaderSide::center;
        persistent->prepare (sr, block, chans);
        graph.mutate ([&] (std::vector<std::shared_ptr<Channel>>& v) { v.push_back (persistent); });
    }

    std::atomic<bool> running { true };
    std::atomic<bool> churnFailed { false };

    // "Message thread": add/remove/reorder silent churn channels continuously.
    std::thread churn ([&]
    {
        juce::Random rng (0x5713);
        while (running.load (std::memory_order_acquire))
        {
            const int action = rng.nextInt (3);
            graph.mutate ([&] (std::vector<std::shared_ptr<Channel>>& v)
            {
                if (action == 0 && v.size() < (size_t) kMaxChannels)
                    v.push_back (makeSilent());
                else if (action == 1 && v.size() > 1)
                    v.erase (v.begin() + 1 + rng.nextInt ((int) v.size() - 1));   // never index 0 (persistent)
                else if (v.size() > 2)
                    std::swap (v[1], v[1 + rng.nextInt ((int) v.size() - 1)]);
            });
        }
    });

    // "Audio thread": render + verify the persistent sine is never disturbed.
    juce::AudioBuffer<float> master (chans, block);
    juce::int64 sampleIndex = 0;
    double worstErr = 0.0;
    bool ok = true;

    for (juce::int64 b = 0; b < totalBlocks && ok; ++b)
    {
        BlockContext ctx;
        ctx.numSamples = block;
        ctx.leftBusGain = ctx.rightBusGain = 1.0f;
        ctx.blockStartSample = sampleIndex;
        juce::MidiBuffer midi; ctx.midi = &midi;

        master.clear();
        auto snap = graph.current();                 // the one lock-free read
        for (auto& ch : snap->channels)
            if (ch != nullptr)
                ch->processBlock (master, ctx);

        for (int i = 0; i < block && ok; ++i)
        {
            const float want = expectedSample (sampleIndex + i);
            const float got  = master.getSample (0, i);
            if (! std::isfinite (got)) { std::cerr << "FAIL: non-finite at block " << b << "\n"; ok = false; break; }
            const double err = std::abs ((double) got - (double) want);
            worstErr = juce::jmax (worstErr, err);
            if (err > 1.0e-4) { std::cerr << "FAIL: dropout/discontinuity at block " << b
                                          << " sample " << i << " got=" << got << " want=" << want << "\n"; ok = false; break; }
        }
        sampleIndex += block;
    }

    running.store (false, std::memory_order_release);
    churn.join();

    if (! ok || churnFailed.load()) return 1;
    std::cout << "CHANNEL_DROPOUT_OK  blocks=" << totalBlocks
              << "  worst_sample_err=" << worstErr << "\n";
    return 0;
}
