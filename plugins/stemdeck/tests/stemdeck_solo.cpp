// Mute / solo contract.
//
// Solo is a MIX-WIDE decision: soloing one channel must drop every other channel
// out of the master, and un-soloing must restore them exactly. Mute is per-channel
// and independent. This checks the arbitration numerically rather than by eye —
// three channels emitting known constants, so every combination has an exact
// expected sum.
//
//   StemDeckSolo → prints SOLO_OK on success
#include "Channel.h"
#include "GraphSnapshot.h"
#include <iostream>
#include <cmath>

namespace
{
class ConstBody : public ChannelBody
{
public:
    explicit ConstBody (float v) : value (v) {}
    ChannelType type() const noexcept override { return ChannelType::trackDeck; }
    void prepare (double, int, int) override {}
    void processInto (juce::AudioBuffer<float>& scratch, const BlockContext& ctx) noexcept override
    {
        for (int ch = 0; ch < scratch.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill (scratch.getWritePointer (ch), value, ctx.numSamples);
    }
    float value;
};

constexpr int kBlock = 128, kChans = 2;
}

int main()
{
    ChannelGraph graph;
    std::vector<std::shared_ptr<Channel>> chans;

    // Distinct constants so any subset sums to a unique, checkable value.
    const float vals[3] { 0.1f, 0.2f, 0.4f };
    for (int i = 0; i < 3; ++i)
    {
        auto c = std::make_shared<Channel> (i + 1, std::make_unique<ConstBody> (vals[i]));
        c->cfSide = CrossfaderSide::center;      // bus gain 1.0, isolates mute/solo
        c->prepare (44100.0, kBlock, kChans);
        chans.push_back (c);
    }
    graph.mutate ([&] (std::vector<std::shared_ptr<Channel>>& v) { v = chans; });

    // Renders one block the way the processor does: resolve anySoloed, then walk.
    auto render = [&]
    {
        BlockContext ctx;
        ctx.numSamples = kBlock;
        ctx.leftBusGain = ctx.rightBusGain = 1.0f;
        juce::MidiBuffer midi; ctx.midi = &midi;

        auto snap = graph.current();
        ctx.anySoloed = false;
        for (auto& c : snap->channels)
            if (c != nullptr && c->isSoloed()) { ctx.anySoloed = true; break; }

        juce::AudioBuffer<float> master (kChans, kBlock);
        master.clear();
        for (auto& c : snap->channels)
            if (c != nullptr) c->processBlock (master, ctx);
        return master.getSample (0, kBlock / 2);
    };

    auto expect = [] (const char* what, float got, float want)
    {
        if (std::abs (got - want) > 1.0e-5f)
        { std::cerr << "FAIL: " << what << " = " << got << ", expected " << want << "\n"; return false; }
        return true;
    };

    // 1) Nothing engaged -> everything sums.
    if (! expect ("no mute/solo", render(), 0.7f)) return 1;

    // 2) Solo one -> ONLY that channel survives.
    chans[1]->setSoloed (true);
    if (! expect ("solo ch1", render(), 0.2f)) return 1;

    // 3) Solo two -> both survive, the third drops.
    chans[2]->setSoloed (true);
    if (! expect ("solo ch1+ch2", render(), 0.6f)) return 1;

    // 4) Un-solo everything -> exact restoration (no residual state).
    chans[1]->setSoloed (false);
    chans[2]->setSoloed (false);
    if (! expect ("solo cleared", render(), 0.7f)) return 1;

    // 5) Mute is independent of solo.
    chans[0]->setMuted (true);
    if (! expect ("mute ch0", render(), 0.6f)) return 1;

    // 6) Mute WINS over solo: a muted channel stays silent even when soloed.
    chans[0]->setSoloed (true);
    if (! expect ("muted+soloed ch0", render(), 0.0f)) return 1;

    // 7) Unmute it while still soloed -> it alone returns.
    chans[0]->setMuted (false);
    if (! expect ("soloed ch0 unmuted", render(), 0.1f)) return 1;

    std::cout << "SOLO_OK  mix-wide arbitration verified\n";
    return 0;
}
