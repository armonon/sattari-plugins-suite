// SessionRenderer check: render a captured session to a stereo mixdown and assert
// it is deterministic, finite, correctly gated by faders/transport, and re-times
// with the master tempo. Proves the export payoff (plan -> audio) without ears —
// structure + determinism, not subjective mix quality.
#include "SessionRenderer.h"
#include "SessionCapture.h"
#include <iostream>
#include <string>
#include <cmath>
#include <map>

using namespace stemdeck::session;

namespace
{
int fails = 0;
void check (bool ok, const std::string& what)
{
    std::cout << "  " << (ok ? "OK  " : "FAIL") << "  " << what << "\n";
    if (! ok) ++fails;
}

double rms (const juce::AudioBuffer<float>& b)
{
    double s = 0.0; const int n = b.getNumSamples();
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < n; ++i) { const double v = b.getSample (ch, i); s += v * v; }
    return std::sqrt (s / juce::jmax (1, b.getNumChannels() * n));
}
bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i))) return false;
    return true;
}

// A 220 Hz sine "track" the provider hands back for any trackId.
juce::AudioBuffer<float> sineTrack (double sr, double secs, double hz)
{
    const int n = (int) (sr * secs);
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * (float) hz * (float) i / (float) sr);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}

SessionModel performWith (double tempo, double fader)
{
    SessionCapture cap; cap.begin (48000.0, tempo);
    TrackRef a { "A", "/m/a.wav", "ha", 30.0, tempo, 0.0, 7, true };
    cap.recordDeckLoad (0, a, 0.0);
    cap.recordParam (0, "fader", fader, 0.0);
    cap.recordTransport (0, TransportKind::play, 0.0, 0.0);
    cap.recordTransport (0, TransportKind::pause, 0.0, 16.0);   // stop at beat 16
    return cap.session();
}
} // namespace

int main()
{
    const double SR = 48000.0;
    auto track = sineTrack (SR, 30.0, 220.0);
    SessionRenderer::AudioProvider provider =
        [&] (const juce::String&, double& rate) { rate = SR; return &track; };

    // ── Renders non-silent, finite audio of a sane length ───────────────────────
    {
        const auto s = performWith (124.0, 1.0);
        const auto out = SessionRenderer::render (s, provider);
        check (allFinite (out), "render output is finite (no NaN/inf)");
        check (rms (out) > 0.01, "render is non-silent with fader up");
        // last event at beat 16 + 8-beat tail = 24 beats @124 -> ~ that many samples
        const int expect = (int) (24.0 * (60.0 / 124.0) * SR);
        check (std::abs (out.getNumSamples() - expect) < (int) SR, "render length ~ last event + tail");
    }

    // ── Determinism: identical output on a second render ────────────────────────
    {
        const auto s = performWith (124.0, 1.0);
        const auto a = SessionRenderer::render (s, provider);
        const auto b = SessionRenderer::render (s, provider);
        bool same = a.getNumSamples() == b.getNumSamples();
        for (int i = 0; same && i < a.getNumSamples(); ++i)
            same = a.getSample (0, i) == b.getSample (0, i) && a.getSample (1, i) == b.getSample (1, i);
        check (same, "render is bit-deterministic across runs");
    }

    // ── Fader gates the mix: 0 -> silent, and gain is monotonic ─────────────────
    {
        const auto silent = SessionRenderer::render (performWith (124.0, 0.0), provider);
        check (rms (silent) < 1e-6, "fader 0 -> silent render");
        const auto quiet = SessionRenderer::render (performWith (124.0, 0.25), provider);
        const auto loud  = SessionRenderer::render (performWith (124.0, 1.0),  provider);
        check (rms (loud) > rms (quiet) * 2.0, "higher fader -> proportionally louder");
    }

    // ── Transport gates content: audio only while playing ───────────────────────
    {
        const auto s = performWith (124.0, 1.0);
        const auto out = SessionRenderer::render (s, provider);
        // beat 16 (pause) in samples; region after should be (near) silent tail.
        const int pauseSamp = (int) (16.0 * (60.0 / 124.0) * SR);
        double before = 0, after = 0; int nb = 0, na = 0;
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const double v = std::abs (out.getSample (0, i));
            if (i < pauseSamp - 2000) { before += v; ++nb; }
            else if (i > pauseSamp + 2000) { after += v; ++na; }
        }
        check (nb > 0 && before / nb > 0.01, "audio present before pause");
        check (na > 0 && after / na < 1e-4, "silence after pause (transport gates the mix)");
    }

    // ── Tempo re-export changes render length, stays non-silent ─────────────────
    {
        const auto slow = SessionRenderer::render (performWith (124.0, 1.0), provider);
        const auto fast = SessionRenderer::render (performWith (128.0, 1.0), provider);
        check (fast.getNumSamples() < slow.getNumSamples(), "128 BPM render is shorter than 124 (events re-timed)");
        check (rms (fast) > 0.01, "faster render still non-silent");
    }

    std::cout << (fails == 0 ? "RENDERER_OK" : "RENDERER_FAIL (" + std::to_string (fails) + ")") << "\n";
    return fails == 0 ? 0 : 1;
}
