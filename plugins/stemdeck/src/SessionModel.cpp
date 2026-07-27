#include "SessionModel.h"
#include "StemGain.h"
#include <algorithm>

namespace stemdeck::session
{

namespace
{
const char* eventTypeName (EventType t)
{
    switch (t) { case EventType::deckLoad: return "deckLoad";
                 case EventType::transport: return "transport";
                 default: return "param"; }
}
EventType eventTypeFrom (const juce::String& s)
{
    if (s == "deckLoad")  return EventType::deckLoad;
    if (s == "transport") return EventType::transport;
    return EventType::param;
}
const char* transportName (TransportKind k)
{
    switch (k) { case TransportKind::play: return "play"; case TransportKind::pause: return "pause";
                 case TransportKind::cue: return "cue"; default: return "seek"; }
}
TransportKind transportFrom (const juce::String& s)
{
    if (s == "play")  return TransportKind::play;
    if (s == "pause") return TransportKind::pause;
    if (s == "cue")   return TransportKind::cue;
    return TransportKind::seek;
}
} // namespace

void SessionModel::sortEvents()
{
    std::stable_sort (events.begin(), events.end(),
                      [] (const Event& a, const Event& b) { return a.atBeat < b.atBeat; });
}

juce::int64 SessionModel::addEvent (Event e)
{
    if (e.id == 0) e.id = nextEventId++;
    else nextEventId = juce::jmax (nextEventId, e.id + 1);
    events.push_back (std::move (e));
    sortEvents();
    return events.empty() ? 0 : e.id;
}

void SessionModel::addTrack (TrackRef t) { tracks.push_back (std::move (t)); }

juce::int64 SessionModel::addRegion (Region r)
{
    if (r.id == 0) r.id = nextRegionId++;
    else nextRegionId = juce::jmax (nextRegionId, r.id + 1);
    const auto id = r.id;
    regions.push_back (std::move (r));
    return id;
}

Event* SessionModel::findEvent (juce::int64 id)
{
    for (auto& e : events) if (e.id == id) return &e;
    return nullptr;
}
Region* SessionModel::findRegion (juce::int64 id)
{
    for (auto& r : regions) if (r.id == id) return &r;
    return nullptr;
}

void SessionModel::moveRegion (juce::int64 regionId, double deltaBeats)
{
    auto* r = findRegion (regionId);
    if (r == nullptr) return;
    for (auto eid : r->eventIds)
        if (auto* e = findEvent (eid)) e->atBeat += deltaBeats;
    r->startBeat += deltaBeats;
    r->endBeat   += deltaBeats;
    sortEvents();
}

void SessionModel::stretchRegion (juce::int64 regionId, double newLengthBeats)
{
    auto* r = findRegion (regionId);
    if (r == nullptr || newLengthBeats <= 0.0) return;
    const double oldLen = r->endBeat - r->startBeat;
    if (oldLen <= 1.0e-9) { r->endBeat = r->startBeat + newLengthBeats; return; }
    const double scale = newLengthBeats / oldLen;
    const double s = r->startBeat;
    for (auto eid : r->eventIds)
        if (auto* e = findEvent (eid)) e->atBeat = s + (e->atBeat - s) * scale;
    r->endBeat = s + newLengthBeats;
    sortEvents();
}

// ── Serialisation ────────────────────────────────────────────────────────────
juce::var SessionModel::toVar() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("schemaVersion", kSchemaVersion);
    root->setProperty ("sampleRate", sampleRate);
    root->setProperty ("masterTempo", masterTempo);
    root->setProperty ("createdMs", createdMs);

    juce::Array<juce::var> trackArr;
    for (const auto& t : tracks)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", t.id); o->setProperty ("path", t.path); o->setProperty ("contentHash", t.contentHash);
        o->setProperty ("durationSec", t.durationSec); o->setProperty ("bpm", t.bpm);
        o->setProperty ("downbeatSample", t.downbeatSample);
        o->setProperty ("keyPitchClass", t.keyPitchClass); o->setProperty ("keyIsMinor", t.keyIsMinor);
        trackArr.add (juce::var (o));
    }
    root->setProperty ("tracks", trackArr);

    juce::Array<juce::var> tempoArr;
    for (const auto& tp : tempoLane)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("atBeat", tp.atBeat); o->setProperty ("bpm", tp.bpm);
        tempoArr.add (juce::var (o));
    }
    root->setProperty ("tempoLane", tempoArr);

    juce::Array<juce::var> eventArr;
    for (const auto& e : events)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", e.id); o->setProperty ("atBeat", e.atBeat);
        o->setProperty ("type", eventTypeName (e.type)); o->setProperty ("deck", e.deck);
        if (e.type == EventType::deckLoad)  o->setProperty ("trackId", e.trackId);
        if (e.type == EventType::transport) { o->setProperty ("transport", transportName (e.transport));
                                              o->setProperty ("positionBeat", e.positionBeat); }
        if (e.type == EventType::param)     { o->setProperty ("paramId", e.paramId); o->setProperty ("value", e.value); }
        eventArr.add (juce::var (o));
    }
    root->setProperty ("events", eventArr);

    juce::Array<juce::var> regionArr;
    for (const auto& r : regions)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", r.id); o->setProperty ("kind", r.kind);
        o->setProperty ("startBeat", r.startBeat); o->setProperty ("endBeat", r.endBeat);
        juce::Array<juce::var> ids; for (auto id : r.eventIds) ids.add ((juce::int64) id);
        o->setProperty ("eventIds", ids);
        regionArr.add (juce::var (o));
    }
    root->setProperty ("regions", regionArr);
    return juce::var (root);
}

juce::String SessionModel::toJSON() const { return juce::JSON::toString (toVar(), false); }

SessionModel SessionModel::fromVar (const juce::var& v)
{
    SessionModel s;
    if (auto* root = v.getDynamicObject())
    {
        s.sampleRate  = (double) root->getProperty ("sampleRate");
        s.masterTempo = (double) root->getProperty ("masterTempo");
        s.createdMs   = (juce::int64) root->getProperty ("createdMs");
        if (s.sampleRate  <= 0.0) s.sampleRate = 44100.0;
        if (s.masterTempo <= 0.0) s.masterTempo = 120.0;

        if (auto* ta = root->getProperty ("tracks").getArray())
            for (auto& tv : *ta)
                if (auto* o = tv.getDynamicObject())
                {
                    TrackRef t;
                    t.id = o->getProperty ("id").toString(); t.path = o->getProperty ("path").toString();
                    t.contentHash = o->getProperty ("contentHash").toString();
                    t.durationSec = o->getProperty ("durationSec"); t.bpm = o->getProperty ("bpm");
                    t.downbeatSample = o->getProperty ("downbeatSample");
                    t.keyPitchClass = (int) o->getProperty ("keyPitchClass");
                    t.keyIsMinor = (bool) o->getProperty ("keyIsMinor");
                    s.tracks.push_back (std::move (t));
                }

        if (auto* ea = root->getProperty ("events").getArray())
            for (auto& ev : *ea)
                if (auto* o = ev.getDynamicObject())
                {
                    Event e;
                    e.id = (juce::int64) o->getProperty ("id"); e.atBeat = o->getProperty ("atBeat");
                    e.type = eventTypeFrom (o->getProperty ("type").toString());
                    e.deck = (int) o->getProperty ("deck");
                    e.trackId = o->getProperty ("trackId").toString();
                    e.transport = transportFrom (o->getProperty ("transport").toString());
                    e.positionBeat = o->getProperty ("positionBeat");
                    e.paramId = o->getProperty ("paramId").toString();
                    e.value = o->getProperty ("value");
                    s.nextEventId = juce::jmax (s.nextEventId, e.id + 1);
                    s.events.push_back (std::move (e));
                }
        s.sortEvents();

        // ── Schema migration (ONE-WAY DOOR, see StemGain.h) ─────────────────────
        // v1 recorded stem gain in dB (0.0 == unity); v2 records linear percent
        // (0.0 == silence). Convert on load so an old session replays as performed
        // instead of muting every stem. Sessions with no schemaVersion are v1.
        const int fileVersion = root->hasProperty ("schemaVersion")
                              ? (int) root->getProperty ("schemaVersion") : 1;
        if (fileVersion < 2)
            for (auto& e : s.events)
                if (e.type == EventType::param && isStemGainParamId (e.paramId))
                    e.value = (double) stemdeck::stemgain::legacyDbToPercent ((float) e.value);

        if (auto* ra = root->getProperty ("regions").getArray())
            for (auto& rv : *ra)
                if (auto* o = rv.getDynamicObject())
                {
                    Region r;
                    r.id = (juce::int64) o->getProperty ("id"); r.kind = o->getProperty ("kind").toString();
                    r.startBeat = o->getProperty ("startBeat"); r.endBeat = o->getProperty ("endBeat");
                    if (auto* ids = o->getProperty ("eventIds").getArray())
                        for (auto& idv : *ids) r.eventIds.push_back ((juce::int64) idv);
                    s.nextRegionId = juce::jmax (s.nextRegionId, r.id + 1);
                    s.regions.push_back (std::move (r));
                }

        if (auto* la = root->getProperty ("tempoLane").getArray())
            for (auto& lv : *la)
                if (auto* o = lv.getDynamicObject())
                {
                    const double lb = (double) o->getProperty ("bpm");
                    if (lb > 1.0)   // reject corrupt/0/negative bpm -> avoids inf/NaN sample times
                        s.tempoLane.push_back ({ (double) o->getProperty ("atBeat"), lb });
                }
        std::sort (s.tempoLane.begin(), s.tempoLane.end(),
                   [] (const TempoPoint& a, const TempoPoint& b) { return a.atBeat < b.atBeat; });
    }
    return s;
}

void SessionModel::setTempoPoint (double atBeat, double bpm)
{
    if (bpm <= 1.0) return;
    for (auto& tp : tempoLane)
        if (std::abs (tp.atBeat - atBeat) < 1.0e-6) { tp.bpm = bpm; return; }   // replace at same beat
    tempoLane.push_back ({ atBeat, bpm });
    std::sort (tempoLane.begin(), tempoLane.end(),
               [] (const TempoPoint& a, const TempoPoint& b) { return a.atBeat < b.atBeat; });
}

SessionModel SessionModel::fromJSON (const juce::String& json)
{
    juce::var parsed;
    if (juce::JSON::parse (json, parsed).wasOk())
        return fromVar (parsed);
    return {};
}

bool SessionModel::saveTo (const juce::File& f) const
{
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (toJSON());
}

SessionModel SessionModel::loadFrom (const juce::File& f)
{
    return f.existsAsFile() ? fromJSON (f.loadFileAsString()) : SessionModel {};
}

} // namespace stemdeck::session
