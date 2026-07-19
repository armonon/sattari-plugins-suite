// SessionRender check: a captured session -> deterministic sample-timed plan and
// "deck state at beat N". Proves the schema's render-determinism + tempo-re-export
// contracts end-to-end, and the state-fold the arrange view depends on.
#include "SessionRender.h"
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
bool near (double a, double b, double e = 1e-6) { return std::abs (a - b) < e; }

// Capture a small performance to render from.
SessionModel perform (double tempo)
{
    SessionCapture cap; cap.begin (48000.0, tempo);
    TrackRef a { "A", "/m/a.wav", "ha", 210.0, 124.0, 1649.0, 7, true };
    cap.recordDeckLoad (0, a, 0.0);
    cap.recordTransport (0, TransportKind::play, 0.0, 0.0);
    cap.recordParam (0, "fader", 0.2, 0.0);
    cap.recordParam (0, "fader", 0.8, 16.0);   // ramp up
    cap.recordParam (0, "low",   0.5, 8.0);
    cap.recordTransport (0, TransportKind::pause, 0.0, 64.0);
    return cap.session();
}
} // namespace

int main()
{
    // ── Plan is sample-ordered and matches beat*tempo ───────────────────────────
    {
        const auto s = perform (124.0);
        const auto plan = SessionRender::buildPlan (s);
        check (plan.size() == s.events.size(), "one scheduled action per event");
        bool ordered = true;
        for (size_t i = 1; i < plan.size(); ++i) if (plan[i].atSample < plan[i - 1].atSample) ordered = false;
        check (ordered, "plan is sorted by sample time");
        // beat 16 @ 124 BPM, 48k -> 16 * 60/124 * 48000
        double b16 = 0.0;
        for (const auto& a : plan) if (a.kind == ActionKind::param && a.paramId == "fader" && near (a.value, 0.8)) b16 = a.atSample;
        check (near (b16, 16.0 * (60.0 / 124.0) * 48000.0), "action sample time = beat * 60/tempo * sr");
    }

    // ── Tempo re-export: every action re-times proportionally ───────────────────
    {
        const auto p124 = SessionRender::buildPlan (perform (124.0));
        const auto p128 = SessionRender::buildPlan (perform (128.0));
        check (p124.size() == p128.size(), "same event count across tempi");
        bool scaled = true;
        for (size_t i = 0; i < p124.size(); ++i)
        {
            if (p124[i].atSample == 0.0) { if (p128[i].atSample != 0.0) scaled = false; continue; }
            if (! near (p128[i].atSample / p124[i].atSample, 124.0 / 128.0, 1e-4)) scaled = false;
        }
        check (scaled, "raising tempo 124->128 scales every action's sample time by 124/128");
    }

    // ── Determinism: same session -> byte-identical plan ────────────────────────
    {
        const auto s = perform (124.0);
        const auto p1 = SessionRender::buildPlan (s);
        const auto p2 = SessionRender::buildPlan (s);
        bool same = p1.size() == p2.size();
        for (size_t i = 0; same && i < p1.size(); ++i)
            same = near (p1[i].atSample, p2[i].atSample) && p1[i].paramId == p2[i].paramId && p1[i].value == p2[i].value;
        check (same, "buildPlan is deterministic");
    }

    // ── Deck state at a beat: last-value-wins fold ──────────────────────────────
    {
        const auto s = perform (124.0);
        auto st4  = SessionRender::deckStateAt (s, 0, 4.0);
        check (st4.loaded && st4.trackId == "A" && st4.playing, "at beat 4: loaded track A, playing");
        check (near (st4.param ("fader"), 0.2), "at beat 4: fader still 0.2 (ramp not reached)");
        auto st20 = SessionRender::deckStateAt (s, 0, 20.0);
        check (near (st20.param ("fader"), 0.8) && near (st20.param ("low"), 0.5), "at beat 20: fader 0.8, low 0.5");
        auto st70 = SessionRender::deckStateAt (s, 0, 70.0);
        check (! st70.playing, "at beat 70: paused (transport folded)");
        auto other = SessionRender::deckStateAt (s, 1, 70.0);
        check (! other.loaded, "deck 1 was never loaded");
    }

    std::cout << (fails == 0 ? "RENDER_OK" : "RENDER_FAIL (" + std::to_string (fails) + ")") << "\n";
    return fails == 0 ? 0 : 1;
}
