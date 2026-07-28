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
    // Stamps the ordering fields, applies, and records if capturing. Message thread.
    void dispatch (Event e)
    {
        e.atSample = clockSamples;
        e.seq      = nextSeq++;     // strictly increasing across the whole session
        if (applyFn) applyFn (e);
        if (capturing)
        {
            const std::lock_guard<std::mutex> lock (recordMutex);
            recorded.push_back (e);
        }
    }

    // Convenience for the overwhelmingly common case.
    void dispatchParam (Scope scope, ParamId id, double value,
                        GestureFlag gesture = GestureFlag::none, std::uint32_t index = 0)
    {
        Event e;
        e.type  = EventType::param;
        e.scope = scope;
        e.param.id      = id;
        e.param.value   = value;
        e.param.gesture = gesture;
        e.param.index   = index;
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
        capturing = true;
    }
    void endCapture() { capturing = false; }
    bool isCapturing() const noexcept { return capturing; }

    std::vector<Event> takeRecorded()
    {
        const std::lock_guard<std::mutex> lock (recordMutex);
        auto out = recorded;
        return out;
    }

    // The master clock position events are stamped with. The audio thread advances it;
    // the message thread reads it when stamping.
    void setClockSamples (std::int64_t s) noexcept { clockSamples = s; }
    std::int64_t getClockSamples() const noexcept { return clockSamples; }

    // ── Conversion progress (§11 phase 3) ────────────────────────────────────
    // Which ParamIds actually route through here. Honest reporting matters: a
    // half-converted surface that CLAIMS to be complete produces a replay that is
    // subtly wrong, which is worse than one that admits it is partial.
    static bool ownsParam (ParamId id) noexcept
    {
        // This list must name only what is GENUINELY routed through dispatch in
        // MainPluginProcessor. Listing an unconverted param here would make the
        // conversion look further along than it is, and produce a replay missing
        // gestures it claims to carry.
        switch (id)
        {
            // Direct conversions (no APVTS parameter behind them).
            case ParamId::channelMute:
            case ParamId::channelSolo:
            case ParamId::cfSide:
            // Routed by the APVTS bridge in MainPluginProcessor. Each of these has a
            // mapping in BOTH directions (mapApvtsId / apvtsIdFor); a one-way mapping
            // would capture without replaying, or replay without capturing.
            case ParamId::channelGain:
            case ParamId::eqLow:
            case ParamId::eqMid:
            case ParamId::eqHigh:
            case ParamId::filter:
            case ParamId::pitchSemitones:
            case ParamId::keyLockOn:
            case ParamId::keyTarget:
            case ParamId::bpmSource:
            case ParamId::tempoInterp:
            case ParamId::syncMode:
            case ParamId::gainStem:
            case ParamId::masterGain:
            case ParamId::limiterCeiling:
            case ParamId::masterKey:
            case ParamId::crossfader:
            case ParamId::crossfaderCurve:
            // Body-backed (no APVTS parameter): applied directly in applyEvent.
            case ParamId::inputGain:
            case ParamId::inputMonitor:
            case ParamId::inputSource:
            case ParamId::instGain:
            case ParamId::instMidiChannel:
            case ParamId::insertBypass:
                return true;

            // NOT converted, and each for a REASON rather than an oversight:
            //   stemMute / stemSolo — no engine state exists. WaveformRegion has a
            //     display flag only, so there is nothing to capture or replay yet.
            //   insertParam — a hosted plugin's own parameters. Capturing these means
            //     addressing a third-party parameter index stably across sessions,
            //     which the schema does not yet solve.
            // Naming them here would make a replay look complete while silently
            // dropping their gestures.
            default:
                return false;
        }
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
    std::uint32_t nextSeq = 0;
    std::atomic<bool> capturing { false };
    std::vector<Event> recorded;
    mutable std::mutex recordMutex;
};

} // namespace stemdeck::events
