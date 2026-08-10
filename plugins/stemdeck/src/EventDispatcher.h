#pragma once

// EventDispatcher — A1 phase 3. The single path through which live state changes.
//
// The gate is that replaying a captured stream reproduces the performance. That can
// only hold if the live pass and the replay run the SAME code to change state. So:
//
//        UI control ─┐
//                    ├─> dispatch(e) ─> apply(e) ─> engine
//        replay ─────┘                    ▲
//                                         └── the only mutator
//
// dispatch() additionally records the event when capturing. Replay calls apply()
// directly. Nothing else may mutate the state these events own — a control that
// still pokes APVTS behind dispatch's back is invisible to capture and silently
// absent from the replay, which is exactly the divergence A1 is meant to prevent.
//
// Conversion is incremental and one group at a time: each ParamId moves here, its old
// direct-mutation path is deleted, and `ownsParam` grows. `isFullyConverted()` reports
// honestly on how far that has got rather than implying it is finished.

#include "EventStream.h"
#include <functional>
#include <mutex>
#include <vector>

namespace stemdeck::events
{

class Dispatcher
{
public:
    // Applies an event to the engine. Supplied by the processor; the SAME function is
    // used live and on replay.
    using Apply = std::function<void (const Event&)>;

    explicit Dispatcher (Apply applyFn) : applyFn (std::move (applyFn)) {}

    // ── Live path ────────────────────────────────────────────────────────────
    // Stamps the ordering fields, records if capturing, then applies. Host automation
    // can enter here on the audio thread while UI dispatches on the message thread.
    void dispatch (Event e)
    {
        e.atSample = clockSamples.load (std::memory_order_relaxed);
        e.seq      = nextSeq.fetch_add (1, std::memory_order_relaxed);
        if (capturing.load (std::memory_order_acquire))
        {
            const std::lock_guard<std::mutex> lock (recordMutex);
            // Stop may have won the mutex after our optimistic read.
            if (capturing.load (std::memory_order_relaxed))
            {
                // Capture was pre-reserved at take start. Never grow here: host
                // automation may enter through parameterChanged on the audio thread.
                if (recorded.size() < recorded.capacity())
                    recorded.push_back (e);
                else
                    droppedEvents.fetch_add (1, std::memory_order_relaxed);
            }
        }
        if (applyFn) applyFn (e);
    }

    // Convenience for the overwhelmingly common case.
    void dispatchParam (Scope scope, ParamId id, double value,
                        GestureFlag gesture = GestureFlag::none, std::uint32_t index = 0,
                        std::uint64_t stableId = 0)
    {
        Event e;
        e.type  = EventType::param;
        e.scope = scope;
        e.param.id      = id;
        e.param.value   = value;
        e.param.gesture = gesture;
        e.param.index   = index;
        e.param.stableId = stableId;
        dispatch (std::move (e));
    }

    // ── Replay path ──────────────────────────────────────────────────────────
    // Deliberately does NOT record and does NOT re-stamp: the stream's own ordering
    // is authoritative. Replaying through dispatch() would renumber seq and make the
    // second replay differ from the first.
    void replay (const std::vector<Event>& stream)
    {
        auto sorted = stream;
        std::stable_sort (sorted.begin(), sorted.end());   // (atSample, seq) total order
        for (const auto& e : sorted)
            if (applyFn) applyFn (e);
    }

    // ── Capture control ──────────────────────────────────────────────────────
    void beginCapture()
    {
        const std::lock_guard<std::mutex> lock (recordMutex);
        recorded.clear();
        droppedEvents.store (0, std::memory_order_relaxed);
        recorded.reserve (131072);
        capturing.store (true, std::memory_order_release);
    }
    void endCapture()
    {
        const std::lock_guard<std::mutex> lock (recordMutex);
        capturing.store (false, std::memory_order_release);
    }
    bool isCapturing() const noexcept
    { return capturing.load (std::memory_order_acquire); }

    std::vector<Event> takeRecorded() const
    {
        std::vector<Event> out;
        {
            const std::lock_guard<std::mutex> lock (recordMutex);
            out = recorded;
        }
        std::stable_sort (out.begin(), out.end());   // concurrent producers may acquire the mutex out of seq order
        return out;
    }

    std::size_t recordedEventCount() const
    {
        const std::lock_guard<std::mutex> lock (recordMutex);
        return recorded.size();
    }

    int getDroppedEventCount() const noexcept
    {
        return droppedEvents.load (std::memory_order_relaxed);
    }

    // The master clock position events are stamped with. The audio thread advances it;
    // the message thread reads it when stamping.
    void setClockSamples (std::int64_t s) noexcept { clockSamples = s; }
    std::int64_t getClockSamples() const noexcept { return clockSamples; }

    // ── Conversion progress (§11 phase 3) ────────────────────────────────────
    static bool ownsParam (ParamId id) noexcept
    {
        // ParamId is now a closed vocabulary: every member is either applied
        // directly in applyEvent or has a two-way APVTS mapping. New IDs must land
        // with that mapping in the same change; the dispatch contract exercises the
        // complete [0,count) range.
        const auto raw = static_cast<std::uint16_t> (id);
        return raw < static_cast<std::uint16_t> (ParamId::count);
    }

    static int convertedParamCount() noexcept
    {
        int n = 0;
        for (int i = 0; i < (int) ParamId::count; ++i)
            if (ownsParam ((ParamId) i)) ++n;
        return n;
    }
    static bool isFullyConverted() noexcept
    { return convertedParamCount() == (int) ParamId::count; }

private:
    Apply applyFn;
    std::atomic<std::int64_t> clockSamples { 0 };
    std::atomic<std::uint32_t> nextSeq { 0 };
    std::atomic<bool> capturing { false };
    std::atomic<int> droppedEvents { 0 };
    std::vector<Event> recorded;
    mutable std::mutex recordMutex;
};

} // namespace stemdeck::events
