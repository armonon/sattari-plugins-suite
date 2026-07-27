#pragma once

// =============================================================================
//  SessionModel — StemDeck's capture-intent session format (build-order #2).
// -----------------------------------------------------------------------------
//  See docs/session-schema.md. This is the one-way-door data model: the whole
//  product (arrangement view, transition editing, retroactive mixing, tempo
//  re-export, deterministic render) is views/operations over this.
//
//  Key decision: events are timestamped in BEATS on the master clock, so a tempo
//  change re-times everything for free. Dependency-light (juce_core only) so it
//  is unit-testable standalone and could later be shared.
// =============================================================================

#include <juce_core/juce_core.h>
#include <vector>

namespace stemdeck::session
{

// v2: stem-gain param events changed from dB to linear percent (0=silence,
// 100=unity, 200=x2.0) — see StemGain.h. Sessions written as v1 are migrated on
// load; never reinterpret a v1 value as v2 (0.0 flips from unity to silence).
constexpr int kSchemaVersion = 2;

/** Canonical param id for a stem's gain in the event stream ("stemGain0".."stemGain4"). */
inline juce::String stemGainParamId (int lane) { return "stemGain" + juce::String (lane); }
inline bool isStemGainParamId (const juce::String& id) { return id.startsWith ("stemGain"); }

struct TrackRef
{
    juce::String id, path, contentHash;
    double durationSec = 0.0, bpm = 0.0, downbeatSample = 0.0;
    int  keyPitchClass = -1;
    bool keyIsMinor = false;
};

enum class EventType { deckLoad, transport, param };
enum class TransportKind { play, pause, cue, seek };

struct Event
{
    juce::int64  id = 0;
    double       atBeat = 0.0;          // master musical time
    EventType    type = EventType::param;
    int          deck = 0;

    juce::String  trackId;              // deckLoad
    TransportKind transport = TransportKind::play;
    double        positionBeat = 0.0;   // transport seek target
    juce::String  paramId;              // param
    double        value = 0.0;
};

// A point on the retroactive tempo lane (§5.4): the master tempo is an editable
// automation curve, not a scalar. Between points the tempo is linear; before the
// first / after the last it holds flat. `bpm` is stored ABSOLUTE and UNCLAMPED, so
// a value legitimately set above the performed tempo never clips on replay/edit.
struct TempoPoint
{
    double atBeat = 0.0;
    double bpm    = 120.0;
};

struct Region
{
    juce::int64  id = 0;
    juce::String kind = "transition";
    double       startBeat = 0.0, endBeat = 0.0;
    std::vector<juce::int64> eventIds;
};

class SessionModel
{
public:
    double  sampleRate  = 44100.0;
    double  masterTempo = 120.0;        // BPM
    juce::int64 createdMs = 0;
    std::vector<TrackRef> tracks;
    std::vector<Event>    events;       // kept sorted by atBeat
    std::vector<Region>   regions;
    std::vector<TempoPoint> tempoLane;  // empty => constant masterTempo (§5.4)

    // ── Building ───────────────────────────────────────────────────────────────
    juce::int64 addEvent  (Event e);    // assigns id, inserts in atBeat order, returns id
    void        addTrack  (TrackRef t);
    juce::int64 addRegion (Region r);   // assigns id, returns id

    Event*  findEvent  (juce::int64 id);
    Region* findRegion (juce::int64 id);

    // ── Edit operations (the headline features) ─────────────────────────────────
    // Shift a region and every event it owns by deltaBeats.
    void moveRegion (juce::int64 regionId, double deltaBeats);
    // Rescale a region to newLengthBeats, scaling contained events around startBeat.
    void stretchRegion (juce::int64 regionId, double newLengthBeats);
    // Re-time the whole session by setting the master tempo (no event data changes).
    void setMasterTempo (double bpm) noexcept { masterTempo = bpm > 1.0 ? bpm : masterTempo; }

    // ── Retroactive tempo lane (§5.4) ────────────────────────────────────────────
    // Set/replace the tempo at a beat (keeps the lane sorted). The lane re-times the
    // whole session on render/export without touching a single event's beat position
    // — that is the "performed at 124, drawn to 128, re-times on export" thesis.
    void setTempoPoint (double atBeat, double bpm);
    void clearTempoLane() noexcept { tempoLane.clear(); }

    // Interpolated tempo at a beat (flat outside the lane; masterTempo if empty).
    double bpmAtBeat (double beat) const noexcept
    {
        if (tempoLane.empty()) return masterTempo;
        if (beat <= tempoLane.front().atBeat) return tempoLane.front().bpm;
        if (beat >= tempoLane.back().atBeat)  return tempoLane.back().bpm;
        for (size_t i = 1; i < tempoLane.size(); ++i)
            if (beat <= tempoLane[i].atBeat)
            {
                const auto& a = tempoLane[i - 1]; const auto& b = tempoLane[i];
                const double d = b.atBeat - a.atBeat;
                const double t = d > 0.0 ? (beat - a.atBeat) / d : 0.0;
                return a.bpm + t * (b.bpm - a.bpm);
            }
        return masterTempo;
    }

    // ── Rendering ───────────────────────────────────────────────────────────────
    // Sample time of a beat = integral of 60/tempo(beta) dbeta from 0..beat. With a
    // piecewise-linear tempo lane this is exact per segment; empty lane collapses to
    // the old constant-tempo formula.
    double sampleTimeForBeat (double beat) const noexcept
    {
        if (tempoLane.empty())
            return beat * (60.0 / masterTempo) * sampleRate;

        auto segTime = [] (double beatsSpan, double t0, double t1) -> double
        {
            if (beatsSpan <= 0.0) return 0.0;
            if (std::abs (t1 - t0) < 1.0e-9) return 60.0 * beatsSpan / t0;
            return 60.0 * beatsSpan / (t1 - t0) * std::log (t1 / t0);   // linear-tempo segment
        };

        double timeSec = 0.0, prevBeat = 0.0;
        auto step = [&] (double toBeat)
        {
            if (toBeat <= prevBeat) return;
            timeSec += segTime (toBeat - prevBeat, bpmAtBeat (prevBeat), bpmAtBeat (toBeat));
            prevBeat = toBeat;
        };
        for (const auto& p : tempoLane)
        {
            if (p.atBeat >= beat) break;
            step (p.atBeat);
        }
        step (beat);
        return timeSec * sampleRate;
    }

    // ── Serialisation (deterministic round-trip) ────────────────────────────────
    juce::var    toVar()  const;
    juce::String toJSON() const;                       // stable output
    static SessionModel fromVar (const juce::var&);
    static SessionModel fromJSON (const juce::String&); // throws-free; empty on parse error

    bool saveTo (const juce::File&) const;
    static SessionModel loadFrom (const juce::File&);

private:
    juce::int64 nextEventId = 1, nextRegionId = 1;
    void sortEvents();
};

} // namespace stemdeck::session
