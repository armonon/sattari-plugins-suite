#include "SessionCapture.h"

namespace stemdeck::session
{

void SessionCapture::begin (double sampleRate, double masterTempo)
{
    model = SessionModel {};
    model.sampleRate  = sampleRate  > 0.0 ? sampleRate  : 44100.0;
    model.masterTempo = masterTempo > 1.0 ? masterTempo : 120.0;
    model.createdMs   = juce::Time::currentTimeMillis();
    capturing = true;
}

void SessionCapture::recordDeckLoad (int deck, const TrackRef& track, double beatNow)
{
    if (! capturing) return;

    // Add to the pool if this track id isn't already present.
    bool present = false;
    for (const auto& t : model.tracks) if (t.id == track.id) { present = true; break; }
    if (! present) model.addTrack (track);

    Event e; e.type = EventType::deckLoad; e.deck = deck; e.atBeat = beatNow; e.trackId = track.id;
    model.addEvent (e);
}

void SessionCapture::recordTransport (int deck, TransportKind kind, double positionBeat, double beatNow)
{
    if (! capturing) return;
    Event e; e.type = EventType::transport; e.deck = deck; e.atBeat = beatNow;
    e.transport = kind; e.positionBeat = positionBeat;
    model.addEvent (e);
}

void SessionCapture::recordParam (int deck, const juce::String& paramId, double value, double beatNow)
{
    if (! capturing) return;

    // Coalesce a repeat of the same param on the same deck at the same beat
    // (e.g. a knob emitting several sub-sample updates within one UI tick) — keep
    // the latest value instead of stacking duplicate events.
    for (auto it = model.events.rbegin(); it != model.events.rend(); ++it)
    {
        if (it->atBeat < beatNow - 1.0e-9) break;          // events are sorted; past this beat, stop
        if (it->type == EventType::param && it->deck == deck && it->paramId == paramId
            && std::abs (it->atBeat - beatNow) < 1.0e-9)
        {
            it->value = value;
            return;
        }
    }

    Event e; e.type = EventType::param; e.deck = deck; e.atBeat = beatNow;
    e.paramId = paramId; e.value = value;
    model.addEvent (e);
}

} // namespace stemdeck::session
