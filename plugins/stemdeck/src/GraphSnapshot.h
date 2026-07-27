#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// GraphSnapshot — the immutable channel list the audio thread iterates each block.
//
// Zero-dropout add/remove (spec §3.1) is achieved WITHOUT locks: the message thread
// builds a NEW snapshot (copy the vector, add/remove/reorder), then publishes it via
// std::atomic_store. The audio thread does one std::atomic_load per block and walks a
// valid, immutable list — it never sees a half-built graph. This reuses the exact
// atomic-shared_ptr idiom already proven in DeckPlayer::setStemAudio/getStemAudio.
//
// Retired channels stay alive inside the old snapshot until it is released, so a
// channel removed mid-block is never destroyed while the audio thread might touch it
// (deferred reclamation — the ChannelGraph holds the previous snapshot one cycle).
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <atomic>
#include "Channel.h"

struct GraphSnapshot
{
    // shared_ptr<const Channel>: channels are shared between snapshots (an unchanged
    // channel isn't rebuilt on add/remove of a sibling) and outlive any snapshot that
    // still references them.
    std::vector<std::shared_ptr<Channel>> channels;
};

// Owns the published snapshot and mediates all mutation. Audio thread: current().
// Message thread: publish() with a freshly built list.
class ChannelGraph
{
public:
    ChannelGraph()
        : live (std::make_shared<GraphSnapshot>()) {}

    // AUDIO THREAD — RT-safe: one atomic load, returns an immutable snapshot to walk.
    // Uses the same non-explicit std::atomic_load on shared_ptr proven in DeckPlayer.
    std::shared_ptr<const GraphSnapshot> current() const noexcept
    {
        return std::atomic_load (&live);
    }

    // MESSAGE THREAD — atomically swap in a new snapshot. Keep the outgoing one alive
    // for one cycle so the audio thread can finish any in-flight block against it
    // before its channels are reclaimed.
    void publish (std::shared_ptr<const GraphSnapshot> next) noexcept
    {
        previous = std::atomic_load (&live);
        std::atomic_store (&live, std::move (next));
    }

    // MESSAGE THREAD — build the next snapshot from the current one via an editor
    // callback (add/remove/reorder), then publish it.
    void mutate (const std::function<void (std::vector<std::shared_ptr<Channel>>&)>& edit)
    {
        auto cur  = std::atomic_load (&live);
        auto next = std::make_shared<GraphSnapshot>();
        next->channels = cur->channels;   // copy the shared_ptr vector (cheap)
        edit (next->channels);
        publish (std::move (next));
    }

private:
    std::shared_ptr<const GraphSnapshot> live;
    std::shared_ptr<const GraphSnapshot> previous;   // deferred reclamation of retired channels

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelGraph)
};
