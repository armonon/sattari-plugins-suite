// SessionModel schema check. Proves the capture format supports each headline
// feature as an operation: transitions-as-objects (move/stretch a region re-times
// the events it owns), tempo re-export (beats -> sample time recomputes), and
// session integrity (deterministic JSON round-trip).
#include "SessionModel.h"
#include <iostream>
#include <string>
#include <cmath>

using namespace stemdeck::session;

namespace
{
int fails = 0;
void check (bool ok, const std::string& what)
{
    std::cout << "  " << (ok ? "OK  " : "FAIL") << "  " << what << "\n";
    if (! ok) ++fails;
}
bool near (double a, double b, double eps = 1e-9) { return std::abs (a - b) < eps; }

// A tiny session: deck A loaded, a fader move, and a transition region [32,64]
// containing two param events at beats 40 and 56.
SessionModel buildSession()
{
    SessionModel s; s.sampleRate = 48000.0; s.masterTempo = 124.0; s.createdMs = 1000;
    s.addTrack ({ "t1", "/music/a.wav", "hashA", 210.0, 124.0, 1649.0, 7, true });

    Event load; load.type = EventType::deckLoad; load.deck = 0; load.atBeat = 0.0; load.trackId = "t1";
    s.addEvent (load);

    Event f1; f1.type = EventType::param; f1.deck = 0; f1.atBeat = 40.0; f1.paramId = "fader"; f1.value = 0.8;
    Event f2; f2.type = EventType::param; f2.deck = 0; f2.atBeat = 56.0; f2.paramId = "fader"; f2.value = 0.2;
    const auto id1 = s.addEvent (f1);
    const auto id2 = s.addEvent (f2);

    Region tr; tr.kind = "transition"; tr.startBeat = 32.0; tr.endBeat = 64.0; tr.eventIds = { id1, id2 };
    s.addRegion (tr);
    return s;
}
} // namespace

int main()
{
    // ── Tempo re-export: same beats, different wall-clock ───────────────────────
    {
        SessionModel s = buildSession();
        const double at124 = s.sampleTimeForBeat (64.0);
        s.setMasterTempo (128.0);
        const double at128 = s.sampleTimeForBeat (64.0);
        check (near (at124, 64.0 * (60.0 / 124.0) * 48000.0), "sampleTimeForBeat @124 correct");
        check (at128 < at124 && near (at128 / at124, 124.0 / 128.0), "raising tempo pulls the render earlier proportionally");
        check (s.events.size() == 3, "tempo change edits no event data");
    }

    // ── Transition as an object: move re-times contained events ─────────────────
    {
        SessionModel s = buildSession();
        const auto rid = s.regions.front().id;
        s.moveRegion (rid, +16.0);
        auto* r = s.findRegion (rid);
        check (r && near (r->startBeat, 48.0) && near (r->endBeat, 80.0), "moveRegion shifts the region +16");
        // the two contained fader events were at 40 and 56 -> now 56 and 72
        int moved = 0;
        for (auto& e : s.events) if (e.paramId == "fader") { if (near (e.atBeat, 56.0) || near (e.atBeat, 72.0)) ++moved; }
        check (moved == 2, "moveRegion re-times the events it owns (40->56, 56->72)");
        // an event NOT in the region (the deckLoad at 0) must be untouched
        for (auto& e : s.events) if (e.type == EventType::deckLoad) check (near (e.atBeat, 0.0), "events outside the region are untouched");
    }

    // ── Transition as an object: stretch scales contained events around start ────
    {
        SessionModel s = buildSession();
        const auto rid = s.regions.front().id;
        s.stretchRegion (rid, 64.0);                 // 32-beat region -> 64 beats (x2)
        auto* r = s.findRegion (rid);
        check (r && near (r->endBeat, 96.0), "stretchRegion 32->64 sets end to start+64 (96)");
        // events at offset 8 and 24 from start(32) -> offsets 16 and 48 -> beats 48 and 80
        int scaled = 0;
        for (auto& e : s.events) if (e.paramId == "fader") { if (near (e.atBeat, 48.0) || near (e.atBeat, 80.0)) ++scaled; }
        check (scaled == 2, "stretchRegion scales contained events around the region start");
    }

    // ── Session integrity: deterministic round-trip ─────────────────────────────
    {
        SessionModel s = buildSession();
        const auto json1 = s.toJSON();
        auto reparsed = SessionModel::fromJSON (json1);
        const auto json2 = reparsed.toJSON();
        check (json1 == json2, "toJSON/fromJSON is a deterministic round-trip");
        check (reparsed.events.size() == s.events.size()
               && reparsed.regions.size() == s.regions.size()
               && reparsed.tracks.size() == s.tracks.size(), "round-trip preserves object counts");
        check (near (reparsed.masterTempo, 124.0) && near (reparsed.sampleRate, 48000.0), "round-trip preserves session scalars");
        auto* r = reparsed.findRegion (reparsed.regions.front().id);
        check (r && r->eventIds.size() == 2, "round-trip preserves region -> event ownership");
    }

    // ── Parse safety ────────────────────────────────────────────────────────────
    {
        const auto empty = SessionModel::fromJSON ("not json {{{");
        check (empty.events.empty() && empty.masterTempo == 120.0, "malformed JSON yields a safe empty session");
    }

    std::cout << (fails == 0 ? "SESSION_OK" : "SESSION_FAIL (" + std::to_string (fails) + ")") << "\n";
    return fails == 0 ? 0 : 1;
}
