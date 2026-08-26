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
                 case EventType::param: return "param"; }
    return "param";
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
                 case TransportKind::cue: return "cue"; case TransportKind::seek: return "seek"; }
    return "seek";
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
    if (! std::isfinite (e.atBeat)) e.atBeat = 0.0;
    if (! std::isfinite (e.positionBeat)) e.positionBeat = 0.0;
    if (! std::isfinite (e.value)) e.value = 0.0;
    e.atBeat = juce::jmax (0.0, e.atBeat);
    e.positionBeat = juce::jmax (0.0, e.positionBeat);
    if (e.id == 0) e.id = nextEventId++;
    else nextEventId = juce::jmax (nextEventId, e.id + 1);
    const auto id = e.id;
    events.push_back (std::move (e));
    sortEvents();
    return id;
}

void SessionModel::addTrack (TrackRef t)
{
    if (! std::isfinite (t.durationSec) || t.durationSec < 0.0)
        t.durationSec = 0.0;
    if (! std::isfinite (t.bpm) || t.bpm < 0.0)
        t.bpm = 0.0;
    if (! std::isfinite (t.downbeatSample) || t.downbeatSample < 0.0)
        t.downbeatSample = 0.0;
    t.keyPitchClass = juce::jlimit (-1, 11, t.keyPitchClass);
    tracks.push_back (std::move (t));
}

juce::int64 SessionModel::addRegion (Region r)
{
    if (! std::isfinite (r.startBeat)) r.startBeat = 0.0;
    if (! std::isfinite (r.endBeat)) r.endBeat = r.startBeat;
    r.startBeat = juce::jmax (0.0, r.startBeat);
    r.endBeat = juce::jmax (r.startBeat, r.endBeat);
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
    if (r == nullptr || ! std::isfinite (deltaBeats)) return;
    double earliest = r->startBeat;
    for (auto eid : r->eventIds)
        if (const auto* e = findEvent (eid))
            earliest = juce::jmin (earliest, e->atBeat);
    deltaBeats = juce::jmax (deltaBeats, -earliest);
    for (auto eid : r->eventIds)
        if (auto* e = findEvent (eid)) e->atBeat += deltaBeats;
    r->startBeat += deltaBeats;
    r->endBeat   += deltaBeats;
    sortEvents();
}

void SessionModel::stretchRegion (juce::int64 regionId, double newLengthBeats)
{
    auto* r = findRegion (regionId);
    if (r == nullptr || ! std::isfinite (newLengthBeats)
        || newLengthBeats <= 0.0) return;
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
        const int fileVersion = root->hasProperty ("schemaVersion")
                              ? (int) root->getProperty ("schemaVersion") : 1;
        // An older build must never reinterpret a future schema and then save a
        // damaged downgrade. Return the safe empty model; the project-level loader
        // reports the newer-version error to the user before replacing live state.
        if (fileVersion > kSchemaVersion)
            return s;

        s.sampleRate  = (double) root->getProperty ("sampleRate");
        s.masterTempo = (double) root->getProperty ("masterTempo");
        s.createdMs   = (juce::int64) root->getProperty ("createdMs");
        if (! std::isfinite (s.sampleRate) || s.sampleRate <= 0.0)
            s.sampleRate = 44100.0;
        if (! std::isfinite (s.masterTempo) || s.masterTempo <= 1.0)
            s.masterTempo = 120.0;

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
                    s.addTrack (std::move (t));
                }

        if (auto* ea = root->getProperty ("events").getArray())
            for (auto& ev : *ea)
                if (auto* o = ev.getDynamicObject())
                {
                    Event e;
                    e.id = (juce::int64) o->getProperty ("id");
                    e.atBeat = (double) o->getProperty ("atBeat");
                    if (! std::isfinite (e.atBeat)) e.atBeat = 0.0;
                    e.atBeat = juce::jmax (0.0, e.atBeat);
                    e.type = eventTypeFrom (o->getProperty ("type").toString());
                    e.deck = (int) o->getProperty ("deck");
                    e.trackId = o->getProperty ("trackId").toString();
                    e.transport = transportFrom (o->getProperty ("transport").toString());
                    e.positionBeat = (double) o->getProperty ("positionBeat");
                    if (! std::isfinite (e.positionBeat)) e.positionBeat = 0.0;
                    e.positionBeat = juce::jmax (0.0, e.positionBeat);
                    e.paramId = o->getProperty ("paramId").toString();
                    e.value = (double) o->getProperty ("value");
                    if (! std::isfinite (e.value)) e.value = 0.0;
                    s.nextEventId = juce::jmax (s.nextEventId, e.id + 1);
                    s.events.push_back (std::move (e));
                }
        s.sortEvents();

        // ── Schema migration (ONE-WAY DOOR, see StemGain.h) ─────────────────────
        // v1 recorded stem gain in dB (0.0 == unity); v2 records linear percent
        // (0.0 == silence). Convert on load so an old session replays as performed
        // instead of muting every stem. Sessions with no schemaVersion are v1.
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
                    r.startBeat = (double) o->getProperty ("startBeat");
                    r.endBeat = (double) o->getProperty ("endBeat");
                    if (! std::isfinite (r.startBeat)) r.startBeat = 0.0;
                    if (! std::isfinite (r.endBeat)) r.endBeat = r.startBeat;
                    r.startBeat = juce::jmax (0.0, r.startBeat);
                    r.endBeat = juce::jmax (r.startBeat, r.endBeat);
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
                    const double at = (double) o->getProperty ("atBeat");
                    if (std::isfinite (lb) && lb > 1.0 && std::isfinite (at))
                        s.tempoLane.push_back ({ juce::jmax (0.0, at), lb });
                }
        std::stable_sort (s.tempoLane.begin(), s.tempoLane.end(),
                          [] (const TempoPoint& a, const TempoPoint& b)
                          { return a.atBeat < b.atBeat; });
        std::vector<TempoPoint> uniqueTempo;
        uniqueTempo.reserve (s.tempoLane.size());
        for (const auto& point : s.tempoLane)
        {
            if (! uniqueTempo.empty()
                && std::abs (uniqueTempo.back().atBeat - point.atBeat) < 1.0e-9)
                uniqueTempo.back() = point;
            else
                uniqueTempo.push_back (point);
        }
        s.tempoLane = std::move (uniqueTempo);
    }
    return s;
}

void SessionModel::setTempoPoint (double atBeat, double bpm)
{
    if (! std::isfinite (atBeat) || ! std::isfinite (bpm) || bpm <= 1.0)
        return;
    atBeat = juce::jmax (0.0, atBeat);
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
