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

    // ── Retroactive tempo lane (§5.4): re-times without touching event beats ────
    {
        SessionModel s; s.sampleRate = 48000.0; s.masterTempo = 124.0;
        const double atConst = s.sampleTimeForBeat (64.0);   // constant 124 baseline

        // Draw the tempo up: 124 at beat 0, ramping to 128 by beat 32, holding after.
        s.setTempoPoint (0.0, 124.0);
        s.setTempoPoint (32.0, 128.0);
        const double atLane = s.sampleTimeForBeat (64.0);

        check (atLane < atConst, "raising the tempo lane pulls a later beat EARLIER in time");
        check (near (s.bpmAtBeat (0.0), 124.0) && near (s.bpmAtBeat (32.0), 128.0), "lane interpolates its endpoints");
        check (near (s.bpmAtBeat (16.0), 126.0), "lane interpolates linearly between points");
        check (near (s.bpmAtBeat (100.0), 128.0), "lane holds flat past the last point");
        // Empty lane must equal the old constant-tempo formula exactly.
        SessionModel c; c.sampleRate = 48000.0; c.masterTempo = 124.0;
        check (near (c.sampleTimeForBeat (64.0), 64.0 * (60.0 / 124.0) * 48000.0),
               "empty tempo lane == constant-tempo formula");
        // A flat lane at the master tempo must match the constant formula too.
        SessionModel f; f.sampleRate = 48000.0; f.masterTempo = 124.0;
        f.setTempoPoint (0.0, 124.0); f.setTempoPoint (200.0, 124.0);
        check (near (f.sampleTimeForBeat (64.0), atConst), "flat lane at master tempo matches constant formula");
        // Round-trip preserves the lane.
        auto rt = SessionModel::fromJSON (s.toJSON());
        check (rt.tempoLane.size() == 2 && near (rt.tempoLane.back().bpm, 128.0), "tempo lane survives JSON round-trip");
    }

    // ── Schema v1 -> v2 stem-gain migration (ONE-WAY DOOR) ─────────────────────
    // v1 stored dB (0 == unity); v2 stores percent (0 == silence). A v1 session must
    // migrate, never be reinterpreted, or every stem silently mutes.
    {
        auto v1 = juce::String (R"({"schemaVersion":1,"sampleRate":48000.0,"masterTempo":124.0,"createdMs":0,
            "tracks":[],"regions":[],"events":[
              {"id":1,"atBeat":0.0,"type":"param","deck":0,"trackId":"","transport":"play","positionBeat":0.0,"paramId":"stemGain0","value":0.0},
              {"id":2,"atBeat":1.0,"type":"param","deck":0,"trackId":"","transport":"play","positionBeat":0.0,"paramId":"stemGain1","value":-60.0},
              {"id":3,"atBeat":2.0,"type":"param","deck":0,"trackId":"","transport":"play","positionBeat":0.0,"paramId":"stemGain2","value":12.0},
              {"id":4,"atBeat":3.0,"type":"param","deck":0,"trackId":"","transport":"play","positionBeat":0.0,"paramId":"fader","value":0.0}]})");
        auto s = SessionModel::fromJSON (v1);
        auto valueOf = [&s] (const juce::String& pid) {
            for (auto& e : s.events) if (e.paramId == pid) return e.value;
            return -999.0;
        };
        check (near (valueOf ("stemGain0"), 100.0), "v1 0 dB migrates to 100% (unity), not silence");
        check (valueOf ("stemGain1") < 1.0,          "v1 -60 dB migrates to ~0% (silence)");
        // Note x2.0 is +6.0206 dB, so +6.0 dB is 199.5%; +12 dB is x3.98 and clamps.
        check (near (valueOf ("stemGain2"), 200.0),  "v1 +12 dB clamps to the 200% (x2.0) ceiling");
        check (near (valueOf ("fader"), 0.0),        "non-stem-gain params are left untouched by migration");

        // A v2 session must pass through unchanged (no double migration).
        const auto v2 = s.toJSON();
        auto again = SessionModel::fromJSON (v2);
        auto valueOf2 = [&again] (const juce::String& pid) {
            for (auto& e : again.events) if (e.paramId == pid) return e.value;
            return -999.0;
        };
        check (near (valueOf2 ("stemGain0"), 100.0), "v2 round-trip does not re-migrate stem gains");
    }

    std::cout << (fails == 0 ? "SESSION_OK" : "SESSION_FAIL (" + std::to_string (fails) + ")") << "\n";
    return fails == 0 ? 0 : 1;
}
