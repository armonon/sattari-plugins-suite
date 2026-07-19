#pragma once

// =============================================================================
//  SessionCapture — records a live performance into a SessionModel as an event
//  stream, timestamped in master-clock beats (build-order #2, capture side).
// -----------------------------------------------------------------------------
//  The processor calls these hooks as the performance happens: deck loads,
//  transport actions, and parameter changes. Each is stamped with the current
//  master musical position (`beatNow`), which the CALLER supplies from the
//  transport — keeping the clock external makes capture deterministically
//  testable and keeps this class free of transport dependencies.
//
//  THREADING: this is a message-thread API and does not lock. Parameter changes
//  that originate on the audio thread (automation playback) must be marshalled to
//  the message thread by the caller before being recorded; capture must never be
//  called from the audio callback.
// =============================================================================

#include "SessionModel.h"

namespace stemdeck::session
{

class SessionCapture
{
public:
    // Start a fresh capture. Stamps sample rate + starting master tempo.
    void begin (double sampleRate, double masterTempo);
    // Stop capturing. The captured session remains available via session().
    void end() noexcept { capturing = false; }
    bool isCapturing() const noexcept { return capturing; }

    // Record a deck load. Adds the track to the pool (deduped by id) and emits a
    // deckLoad event referencing it.
    void recordDeckLoad (int deck, const TrackRef& track, double beatNow);
    // Record a transport action (play/pause/cue/seek). positionBeat is the seek
    // target in the track's musical time (0 for play/pause).
    void recordTransport (int deck, TransportKind kind, double positionBeat, double beatNow);
    // Record a parameter change (fader, EQ, filter, stem gain, pitch, key, or a
    // namespaced insert param). Coalesces a same-param same-beat repeat.
    void recordParam (int deck, const juce::String& paramId, double value, double beatNow);

    const SessionModel& session() const noexcept { return model; }
    SessionModel& session() noexcept { return model; }

    bool saveTo (const juce::File& f) const { return model.saveTo (f); }

private:
    SessionModel model;
    bool capturing = false;
};

} // namespace stemdeck::session
