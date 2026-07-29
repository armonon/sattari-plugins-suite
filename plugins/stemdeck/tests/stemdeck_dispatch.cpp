// A1 phase 3: dispatch and replay must be the same code path.
//
// The gate is that replaying a captured stream reproduces the performance. This pins
// the property that makes that possible, at the dispatcher level, before ~40 control
// call sites are converted on top of it:
//
//   * a live gesture and a replayed gesture apply IDENTICALLY, because both go
//     through the same apply() function
//   * replay is order-authoritative: it sorts by (atSample, seq) and does NOT
//     re-stamp, so replaying twice gives the same result
//   * seq is strictly increasing, so same-sample events have a defined order
//   * capture records exactly what was dispatched
//
// It also reports conversion progress honestly rather than implying phase 3 is done.
#include "EventDispatcher.h"
#include <array>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <thread>

using namespace stemdeck::events;

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (! ok) ++failures;
}

// A stand-in for the engine: the state these events own, and nothing else.
struct FakeEngine
{
    std::map<std::pair<int, int>, double> params;   // (paramId, channel) -> value
    std::vector<juce::String> log;

    void apply (const Event& e)
    {
        if (e.type != EventType::param) return;
        const auto key = std::make_pair ((int) e.param.id, (int) e.scope.channel);
        params[key] = e.param.value;

        std::ostringstream os;
        os << (int) e.param.id << "@" << e.scope.channel << "=" << e.param.value
           << "#" << e.seq;
        log.push_back (juce::String (os.str()));
    }

    bool operator== (const FakeEngine& o) const { return params == o.params; }
};
} // namespace

int main()
{
    std::cout << "A1 dispatch/replay equivalence" << std::endl;

    // ── Live pass ────────────────────────────────────────────────────────────
    FakeEngine live;
    Dispatcher liveD ([&live] (const Event& e) { live.apply (e); });
    liveD.beginCapture();

    std::mt19937 rng (12345);   // fixed seed: the test itself must be deterministic
    std::uniform_real_distribution<double> dist (0.0, 1.0);

    Scope chan; chan.kind = ScopeKind::channel;
    for (int i = 0; i < 200; ++i)
    {
        liveD.setClockSamples (i * 512);
        chan.channel = i % 4;
        // Several events at the SAME sample, so tie-breaking is exercised.
        liveD.dispatchParam (chan, ParamId::channelGain, dist (rng), GestureFlag::none);
        liveD.dispatchParam (chan, ParamId::eqLow,       dist (rng));
        if (i % 7 == 0)
            liveD.dispatchParam (chan, ParamId::channelMute, (i / 7) % 2 ? 1.0 : 0.0);
    }
    liveD.endCapture();

    const auto stream = liveD.takeRecorded();
    check (! stream.empty(), "capture recorded events (" + juce::String ((int) stream.size()) + ")");

    // ── Replay into a fresh engine ───────────────────────────────────────────
    FakeEngine replayed;
    Dispatcher replayD ([&replayed] (const Event& e) { replayed.apply (e); });
    replayD.replay (stream);

    check (live == replayed, "replayed state is IDENTICAL to the live state");
    check (live.log.size() == replayed.log.size(),
           "same number of applications (" + juce::String ((int) live.log.size())
           + " vs " + juce::String ((int) replayed.log.size()) + ")");

    // ── Replay is idempotent: twice gives the same answer ────────────────────
    {
        FakeEngine again;
        Dispatcher d2 ([&again] (const Event& e) { again.apply (e); });
        d2.replay (stream);
        check (again == replayed, "replaying twice gives the same result");
        check (again.log == replayed.log, "the application ORDER is identical too");
    }

    // ── Shuffled input replays the same: order comes from the stream ─────────
    {
        auto shuffled = stream;
        std::shuffle (shuffled.begin(), shuffled.end(), std::mt19937 (999));
        FakeEngine fromShuffled;
        Dispatcher d3 ([&fromShuffled] (const Event& e) { fromShuffled.apply (e); });
        d3.replay (shuffled);
        check (fromShuffled == replayed,
               "a SHUFFLED stream replays identically - (atSample, seq) is authoritative");
        check (fromShuffled.log == replayed.log, "and in the same order");
    }

    // ── seq is strictly increasing ───────────────────────────────────────────
    {
        bool increasing = true;
        for (size_t i = 0; i + 1 < stream.size(); ++i)
            increasing = increasing && stream[i].seq < stream[i + 1].seq;
        check (increasing, "seq is strictly increasing across the session");

        bool sameSampleOrdered = true;
        for (size_t i = 0; i + 1 < stream.size(); ++i)
            if (stream[i].atSample == stream[i + 1].atSample)
                sameSampleOrdered = sameSampleOrdered && stream[i].seq < stream[i + 1].seq;
        check (sameSampleOrdered, "events at the same sample have a defined order");
    }

    // ── Replay must not record ───────────────────────────────────────────────
    {
        FakeEngine e4;
        Dispatcher d4 ([&e4] (const Event& ev) { e4.apply (ev); });
        d4.replay (stream);
        check (d4.takeRecorded().empty(), "replay does not re-record (no feedback loop)");
    }

    // ── Endpoint rule (§5): a gesture's end value is what survives ───────────
    {
        FakeEngine e5;
        Dispatcher d5 ([&e5] (const Event& ev) { e5.apply (ev); });
        d5.beginCapture();
        Scope s; s.kind = ScopeKind::channel; s.channel = 1;
        d5.setClockSamples (1000);
        d5.dispatchParam (s, ParamId::channelGain, 0.10, GestureFlag::begin);
        d5.dispatchParam (s, ParamId::channelGain, 0.55, GestureFlag::none);
        d5.dispatchParam (s, ParamId::channelGain, 0.87321, GestureFlag::end);
        d5.endCapture();

        FakeEngine r5;
        Dispatcher d5r ([&r5] (const Event& ev) { r5.apply (ev); });
        d5r.replay (d5.takeRecorded());
        const auto key = std::make_pair ((int) ParamId::channelGain, 1);
        check (r5.params.count (key) && r5.params[key] == 0.87321,
               "the gesture's END value is the one that survives replay");
    }

    // ── Concurrent producers: host automation + UI must not race seq ─────────
    {
        Dispatcher concurrent ([] (const Event&) {});
        concurrent.beginCapture();
        constexpr int numThreads = 4;
        constexpr int perThread = 2000;
        std::array<std::thread, numThreads> producers;
        for (int t = 0; t < numThreads; ++t)
            producers[(size_t) t] = std::thread ([&concurrent, t]
            {
                Scope s;
                s.kind = ScopeKind::channel;
                s.channel = t;
                for (int i = 0; i < perThread; ++i)
                    concurrent.dispatchParam (s, ParamId::channelGain, (double) i);
            });
        for (auto& producer : producers)
            producer.join();
        concurrent.endCapture();

        const auto captured = concurrent.takeRecorded();
        bool strictSeq = captured.size() == (size_t) (numThreads * perThread);
        for (size_t i = 1; i < captured.size(); ++i)
            strictSeq = strictSeq && captured[i - 1].seq < captured[i].seq;
        check (strictSeq,
               "concurrent UI/host producers keep every event in strict total order");
    }

    // ── Honest conversion progress ───────────────────────────────────────────
    {
        const int done  = Dispatcher::convertedParamCount();
        const int total = (int) ParamId::count;
        std::cout << "  ..   phase 3 conversion: " << done << "/" << total
                  << " ParamIds route through dispatch" << std::endl;
        check (done > 0, "at least one control group is converted");
        check (Dispatcher::ownsParam (ParamId::channelMute), "channelMute is converted");
        check (Dispatcher::ownsParam (ParamId::channelSolo), "channelSolo is converted");
        check (Dispatcher::ownsParam (ParamId::cfSide),      "cfSide is converted");
        check (Dispatcher::ownsParam (ParamId::channelGain), "channelGain is converted");
        check (Dispatcher::ownsParam (ParamId::gainStem),    "gainStem is converted");
        check (Dispatcher::ownsParam (ParamId::stemMute),    "stemMute is converted");
        check (Dispatcher::ownsParam (ParamId::stemSolo),    "stemSolo is converted");
        check (Dispatcher::ownsParam (ParamId::masterGain),  "masterGain is converted");
        check (Dispatcher::ownsParam (ParamId::inputGain),    "inputGain is converted");
        check (Dispatcher::ownsParam (ParamId::insertBypass), "insertBypass is converted");

        // The one that remains needs a stable cross-session id for a third-party
        //                       plugin's own parameter, which the schema does not solve
        check (! Dispatcher::ownsParam (ParamId::insertParam),
               "insertParam is NOT converted - needs a stable third-party param id");
        check (! Dispatcher::isFullyConverted(),
               "isFullyConverted() reports FALSE - phase 3 is genuinely incomplete");
    }

    std::cout << (failures == 0 ? "DISPATCH_OK" : "DISPATCH_FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
