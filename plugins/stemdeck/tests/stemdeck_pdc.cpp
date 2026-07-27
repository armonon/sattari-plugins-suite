// Plugin delay compensation contract (spec §3.3).
//
// Two channels emit the SAME impulse. One simulates an FX chain that adds latency
// (its body writes the impulse late); the host compensates the other by the same
// amount. Without PDC the two impulses land at different sample offsets and the mix
// shows two spikes; with PDC they must coincide — one spike at double amplitude.
//
//   StemDeckPdc → prints PDC_OK on success
#include "Channel.h"
#include "GraphSnapshot.h"
#include <iostream>
#include <cmath>

namespace
{
constexpr int kLatency = 64;   // samples of "plugin" latency on the delayed channel

// Emits a single impulse at `offset` within the first block, then silence.
class ImpulseBody : public ChannelBody
{
public:
    explicit ImpulseBody (int offset) : off (offset) {}
    ChannelType type() const noexcept override { return ChannelType::trackDeck; }
    void prepare (double, int, int) override {}
    void processInto (juce::AudioBuffer<float>& scratch, const BlockContext& ctx) noexcept override
    {
        if (fired) return;
        if (off < ctx.numSamples)
        {
            for (int ch = 0; ch < scratch.getNumChannels(); ++ch)
                scratch.setSample (ch, off, 1.0f);
            fired = true;
        }
    }
    int off; bool fired = false;
};

int findSpike (const juce::AudioBuffer<float>& b, int n, float& value)
{
    int best = -1; float bestV = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float v = std::abs (b.getSample (0, i));
        if (v > bestV) { bestV = v; best = i; }
    }
    value = bestV;
    return best;
}
}

int main()
{
    constexpr int block = 512, chans = 2;

    // Channel A: "clean" — impulse at sample 0, compensated by kLatency.
    auto a = std::make_shared<Channel> (1, std::make_unique<ImpulseBody> (0));
    a->cfSide = CrossfaderSide::center;
    a->prepare (44100.0, block, chans);
    a->setCompensationSamples (kLatency);

    // Channel B: "has an FX chain with latency" — its audio emerges kLatency late.
    auto b = std::make_shared<Channel> (2, std::make_unique<ImpulseBody> (kLatency));
    b->cfSide = CrossfaderSide::center;
    b->prepare (44100.0, block, chans);
    b->setCompensationSamples (0);   // already the most-delayed channel

    ChannelGraph graph;
    graph.mutate ([&] (std::vector<std::shared_ptr<Channel>>& v) { v.push_back (a); v.push_back (b); });

    BlockContext ctx;
    ctx.numSamples = block;
    ctx.leftBusGain = ctx.rightBusGain = 1.0f;
    juce::MidiBuffer midi; ctx.midi = &midi;

    juce::AudioBuffer<float> master (chans, block);
    master.clear();
    { auto snap = graph.current(); for (auto& c : snap->channels) c->processBlock (master, ctx); }

    float peak = 0.0f;
    const int at = findSpike (master, block, peak);

    // Both impulses must have landed on the SAME sample (sum = 2.0), at kLatency.
    if (at != kLatency)
    { std::cerr << "FAIL: aligned spike expected at " << kLatency << ", found at " << at << "\n"; return 1; }
    if (std::abs (peak - 2.0f) > 1.0e-4f)
    { std::cerr << "FAIL: impulses did not coincide (peak=" << peak << ", expected 2.0)\n"; return 1; }

    // Sanity: there must be no second, uncompensated spike elsewhere.
    for (int i = 0; i < block; ++i)
        if (i != at && std::abs (master.getSample (0, i)) > 1.0e-4f)
        { std::cerr << "FAIL: stray energy at " << i << " — channels not aligned\n"; return 1; }

    std::cout << "PDC_OK  aligned_at=" << at << "  summed_peak=" << peak << "\n";
    return 0;
}
