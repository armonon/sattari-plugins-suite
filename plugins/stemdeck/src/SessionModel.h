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

constexpr int kSchemaVersion = 1;

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

    // ── Rendering ───────────────────────────────────────────────────────────────
    double sampleTimeForBeat (double beat) const noexcept
    { return beat * (60.0 / masterTempo) * sampleRate; }

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
