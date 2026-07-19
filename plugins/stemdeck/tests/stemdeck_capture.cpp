// SessionCapture check: simulate a short performance (load, transport, param moves
// across beats), then assert the captured SessionModel is a faithful, ordered,
// deduped event stream that saves + reloads. Proves the capture side of the
// one-way door without needing the live processor / GUI.
#include "SessionCapture.h"
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
bool near (double a, double b, double e = 1e-9) { return std::abs (a - b) < e; }
} // namespace

int main()
{
    SessionCapture cap;
    check (! cap.isCapturing(), "starts not capturing");

    cap.begin (48000.0, 124.0);
    check (cap.isCapturing(), "begin() starts capture");

    // Events recorded BEFORE begin should be dropped; after, kept.
    SessionCapture pre; pre.recordParam (0, "fader", 0.5, 0.0);
    check (pre.session().events.empty(), "records are ignored before begin()");

    // A little performance on deck A (0) then B (1).
    TrackRef a { "A", "/m/a.wav", "ha", 210.0, 124.0, 1649.0, 7, true };
    TrackRef b { "B", "/m/b.wav", "hb", 190.0, 126.0, 900.0, 2, false };
    cap.recordDeckLoad (0, a, 0.0);
    cap.recordTransport (0, TransportKind::play, 0.0, 0.0);
    cap.recordParam (0, "fader", 1.0, 4.0);
    cap.recordParam (0, "low",   0.3, 8.0);
    cap.recordDeckLoad (1, b, 16.0);
    cap.recordTransport (1, TransportKind::cue, 0.0, 16.0);
    cap.recordParam (1, "fader", 0.0, 16.0);
    cap.recordParam (1, "fader", 0.5, 24.0);     // crossfade in

    // Coalescing: two updates of the same param at the same beat keep the latest.
    cap.recordParam (0, "fader", 0.9, 32.0);
    cap.recordParam (0, "fader", 0.6, 32.0);     // same deck/param/beat -> overwrites

    const auto& s = cap.session();
    check (s.tracks.size() == 2, "two tracks in the pool");
    check (near (s.masterTempo, 124.0) && near (s.sampleRate, 48000.0), "session scalars stamped at begin");

    // Count event types.
    int loads = 0, transports = 0, params = 0;
    for (const auto& e : s.events)
        (e.type == EventType::deckLoad ? loads : e.type == EventType::transport ? transports : params) += 1;
    check (loads == 2, "two deckLoad events");
    check (transports == 2, "two transport events");
    check (params == 5, "five param events (the same-beat pair coalesced to one)");

    // The coalesced fader event kept the latest value.
    bool coalescedOk = false;
    for (const auto& e : s.events)
        if (e.type == EventType::param && e.deck == 0 && e.paramId == "fader" && near (e.atBeat, 32.0))
            coalescedOk = near (e.value, 0.6);
    check (coalescedOk, "coalesced param keeps the latest value (0.6)");

    // Events are sorted by beat.
    bool sorted = true;
    for (size_t i = 1; i < s.events.size(); ++i) if (s.events[i].atBeat < s.events[i - 1].atBeat) sorted = false;
    check (sorted, "captured events are ordered by beat");

    // Deck-load added the right track reference.
    bool refOk = false;
    for (const auto& e : s.events) if (e.type == EventType::deckLoad && e.deck == 1) refOk = (e.trackId == "B");
    check (refOk, "deckLoad references the loaded track id");

    // Save + reload round-trips.
    const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("stemdeck_capture_" + juce::String (juce::Time::currentTimeMillis()) + ".json");
    check (cap.saveTo (f), "session saves to disk");
    auto reloaded = SessionModel::loadFrom (f);
    check (reloaded.events.size() == s.events.size() && reloaded.tracks.size() == 2, "reloaded session matches");
    f.deleteFile();

    cap.end();
    check (! cap.isCapturing(), "end() stops capture");

    std::cout << (fails == 0 ? "CAPTURE_OK" : "CAPTURE_FAIL (" + std::to_string (fails) + ")") << "\n";
    return fails == 0 ? 0 : 1;
}
