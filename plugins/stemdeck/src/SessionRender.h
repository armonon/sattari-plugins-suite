#pragma once

// =============================================================================
//  SessionRender — turn a captured SessionModel into (a) a deterministic,
//  sample-timed action plan for the export renderer, and (b) a "deck state at
//  beat N" query for the arrangement view. Both the render pipeline (item #11)
//  and the arrange view (item #5) are built on these two operations.
// -----------------------------------------------------------------------------
//  Pure logic over SessionModel; header-only, juce_core only, unit-testable.
//  Determinism contract: same session + same master tempo => identical plan
//  (the export-determinism requirement in the brief).
// =============================================================================

#include "SessionModel.h"
#include <map>
#include <vector>
#include <algorithm>

namespace stemdeck::session
{

enum class ActionKind { deckLoad, play, pause, cue, seek, param };

struct ScheduledAction
{
    double       atSample = 0.0;      // render clock, derived from atBeat * tempo
    ActionKind   kind = ActionKind::param;
    int          deck = 0;
    juce::String trackId;             // deckLoad
    juce::String paramId;             // param
    double       value = 0.0;         // param value, or seek positionBeat
};

// Folded state of one deck at a point in the session — what the arrange view
// draws and what a random-access renderer seeds from.
struct DeckState
{
    bool loaded = false;
    juce::String trackId;
    bool playing = false;
    std::map<juce::String, double> params;   // last-value-wins up to the query beat

    double param (const juce::String& id, double fallback = 0.0) const
    {
        auto it = params.find (id);
        return it != params.end() ? it->second : fallback;
    }
};

class SessionRender
{
public:
    // Ordered, sample-timed plan. Events are already beat-sorted and tempo > 0, so
    // sample time is monotonic in beat and order is preserved; we sort defensively.
    static std::vector<ScheduledAction> buildPlan (const SessionModel& s)
    {
        std::vector<ScheduledAction> plan;
        plan.reserve (s.events.size());
        for (const auto& e : s.events)
        {
            ScheduledAction a;
            a.atSample = s.sampleTimeForBeat (e.atBeat);
            a.deck = e.deck;
            switch (e.type)
            {
                case EventType::deckLoad:  a.kind = ActionKind::deckLoad; a.trackId = e.trackId; break;
                case EventType::transport: a.kind = toKind (e.transport);  a.value = e.positionBeat; break;
                case EventType::param:     a.kind = ActionKind::param; a.paramId = e.paramId; a.value = e.value; break;
            }
            plan.push_back (std::move (a));
        }
        std::stable_sort (plan.begin(), plan.end(),
                          [] (const ScheduledAction& x, const ScheduledAction& y) { return x.atSample < y.atSample; });
        return plan;
    }

    // Fold all events for `deck` with atBeat <= queryBeat into a DeckState.
    static DeckState deckStateAt (const SessionModel& s, int deck, double queryBeat)
    {
        DeckState st;
        for (const auto& e : s.events)                     // events are beat-sorted
        {
            if (e.atBeat > queryBeat + 1.0e-9) break;
            if (e.deck != deck) continue;
            switch (e.type)
            {
                case EventType::deckLoad:  st.loaded = true; st.trackId = e.trackId; break;
                case EventType::transport:
                    if (e.transport == TransportKind::play)  st.playing = true;
                    else if (e.transport == TransportKind::pause || e.transport == TransportKind::cue) st.playing = false;
                    break;                                  // seek doesn't change play state here
                case EventType::param:     st.params[e.paramId] = e.value; break;
            }
        }
        return st;
    }

private:
    static ActionKind toKind (TransportKind k)
    {
        switch (k) { case TransportKind::play: return ActionKind::play;
                     case TransportKind::pause: return ActionKind::pause;
                     case TransportKind::cue: return ActionKind::cue;
                     default: return ActionKind::seek; }
    }
};

} // namespace stemdeck::session
