// Compile + smoke check for the Channel-architecture scaffolding (Step 1a).
// Verifies the headers compile in isolation and the lock-free snapshot mutate/publish
// + a trivial Channel::processBlock behave. This is the seed of the later
// zero-dropout add/remove-under-playback contract test.
//
//   StemDeckChannelCompile   → prints CHANNEL_SCAFFOLD_OK on success
#include "Channel.h"
#include "GraphSnapshot.h"
#include <iostream>
#include <cmath>

namespace
{
// A trivial body that writes a constant so we can see it land in the master sum.
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

bool approx (float a, float b) { return std::abs (a - b) < 1.0e-5f; }
}

int main()
{
    constexpr int block = 256, chans = 2;

    ChannelGraph graph;
    juce::int64 nextId = 1;

    auto makeChannel = [&] (float v, CrossfaderSide side)
    {
        auto ch = std::make_shared<Channel> (nextId++, std::make_unique<ConstBody> (v));
        ch->cfSide = side;
        ch->prepare (44100.0, block, chans);
        return ch;
    };

    // Add two channels via the lock-free mutate path.
    graph.mutate ([&] (std::vector<std::shared_ptr<Channel>>& v) { v.push_back (makeChannel (0.25f, CrossfaderSide::ac)); });
    graph.mutate ([&] (std::vector<std::shared_ptr<Channel>>& v) { v.push_back (makeChannel (0.10f, CrossfaderSide::bd)); });

    // "Audio thread" render against the current immutable snapshot.
    BlockContext ctx;
    ctx.numSamples = block;
    ctx.leftBusGain = 1.0f;   // A/C
    ctx.rightBusGain = 0.5f;  // B/D
    juce::MidiBuffer midi; ctx.midi = &midi;

    juce::AudioBuffer<float> master (chans, block);
    master.clear();
    {
        auto snap = graph.current();
        if (snap->channels.size() != 2) { std::cerr << "FAIL: expected 2 channels\n"; return 1; }
        for (auto& ch : snap->channels) ch->processBlock (master, ctx);
    }

    // Expected: 0.25*1.0 (A/C bus) + 0.10*0.5 (B/D bus) = 0.30 on each sample.
    const float got = master.getSample (0, 0);
    if (! approx (got, 0.30f)) { std::cerr << "FAIL: mix=" << got << " expected 0.30\n"; return 1; }

    // Remove the first channel; snapshot must shrink and the old one stays alive
    // (deferred reclamation) — no crash, no torn read.
    graph.mutate ([] (std::vector<std::shared_ptr<Channel>>& v) { if (! v.empty()) v.erase (v.begin()); });
    if (graph.current()->channels.size() != 1) { std::cerr << "FAIL: expected 1 after remove\n"; return 1; }

    master.clear();
    { auto snap = graph.current(); for (auto& ch : snap->channels) ch->processBlock (master, ctx); }
    if (! approx (master.getSample (0, 0), 0.05f)) { std::cerr << "FAIL: post-remove mix wrong\n"; return 1; }

    std::cout << "CHANNEL_SCAFFOLD_OK\n";
    return 0;
}
