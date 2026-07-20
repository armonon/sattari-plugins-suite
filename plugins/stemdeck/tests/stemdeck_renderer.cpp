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
#include <limits>

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

    // ── Crossfader: A/B bus isolation + constant power ──────────────────────────
    // Deck 0 on bus A, deck 1 on bus B, both playing full. Sweep the crossfader.
    auto twoDeckMix = [&] (double cfValue)
    {
        SessionCapture cap; cap.begin (48000.0, 124.0);
        cap.recordDeckLoad (0, { "A", "/a", "", 30, 124, 0, 0, false }, 0.0);
        cap.recordDeckLoad (1, { "B", "/b", "", 30, 124, 0, 0, false }, 0.0);
        cap.recordParam (0, "cfSide", 0.0, 0.0);   // A / left
        cap.recordParam (1, "cfSide", 2.0, 0.0);   // B / right
        cap.recordParam (0, "crossfader", cfValue, 0.0);  // master
        cap.recordTransport (0, TransportKind::play, 0.0, 0.0);
        cap.recordTransport (1, TransportKind::play, 0.0, 0.0);
        cap.recordTransport (0, TransportKind::pause, 0.0, 16.0);
        cap.recordTransport (1, TransportKind::pause, 0.0, 16.0);
        return SessionRenderer::render (cap.session(), provider);
    };
    {
        const auto atA = twoDeckMix (0.0);   // fully A: deck0 full, deck1 silent
        const auto atB = twoDeckMix (1.0);   // fully B: deck1 full, deck0 silent
        const auto mid = twoDeckMix (0.5);   // both at cos/sin(pi/4)=0.707
        // Both mixes use the same source, so A-only and B-only should have equal RMS.
        check (std::abs (rms (atA) - rms (atB)) < 1e-3, "crossfader full-A and full-B are symmetric");
        check (rms (atA) > 0.01, "crossfader at 0 passes bus A");
        // Constant power: mid RMS of A+B (0.707 each, same signal summed) vs one full.
        check (rms (mid) > rms (atA) * 0.9, "center keeps constant power (both buses at -3 dB sum)");
    }
    {
        // A deck on bus A goes silent when the crossfader is fully at B.
        SessionCapture cap; cap.begin (48000.0, 124.0);
        cap.recordDeckLoad (0, { "A", "/a", "", 30, 124, 0, 0, false }, 0.0);
        cap.recordParam (0, "cfSide", 0.0, 0.0);
        cap.recordParam (0, "crossfader", 1.0, 0.0);   // hard to B
        cap.recordTransport (0, TransportKind::play, 0.0, 0.0);
        cap.recordTransport (0, TransportKind::pause, 0.0, 16.0);
        check (rms (SessionRenderer::render (cap.session(), provider)) < 1e-6,
               "bus-A deck is silent with crossfader hard to B");
    }

    // ── 3-band EQ: killing a band removes its energy, leaves others ─────────────
    auto toneTrack = [&] (double hz) { return sineTrack (SR, 30.0, hz); };
    auto renderTone = [&] (const juce::AudioBuffer<float>& src, double lowDb, double midDb, double highDb)
    {
        SessionRenderer::AudioProvider p = [&] (const juce::String&, double& rate) { rate = SR; return &src; };
        SessionCapture cap; cap.begin (48000.0, 124.0);
        cap.recordDeckLoad (0, { "T", "/t", "", 30, 124, 0, 0, false }, 0.0);
        if (lowDb  != 0.0) cap.recordParam (0, "low",  lowDb,  0.0);
        if (midDb  != 0.0) cap.recordParam (0, "mid",  midDb,  0.0);
        if (highDb != 0.0) cap.recordParam (0, "high", highDb, 0.0);
        cap.recordTransport (0, TransportKind::play, 0.0, 0.0);
        cap.recordTransport (0, TransportKind::pause, 0.0, 16.0);
        return SessionRenderer::render (cap.session(), p);
    };
    {
        auto lowTone  = toneTrack (60.0);     // deep bass
        auto highTone = toneTrack (6000.0);   // treble

        const double lowFlat = rms (renderTone (lowTone, 0, 0, 0));
        const double lowKill = rms (renderTone (lowTone, -60.0, 0, 0));   // kill low band
        check (lowFlat > 0.01, "60 Hz tone renders at flat EQ");
        check (lowKill < lowFlat * 0.1, "killing the LOW band removes a 60 Hz tone");

        const double hiFlat = rms (renderTone (highTone, 0, 0, 0));
        const double hiLowKill = rms (renderTone (highTone, -60.0, 0, 0)); // low kill on a treble tone
        check (hiLowKill > hiFlat * 0.7, "killing the LOW band leaves a 6 kHz tone intact");
        const double hiHighKill = rms (renderTone (highTone, 0, 0, -60.0)); // kill high band
        check (hiHighKill < hiFlat * 0.2, "killing the HIGH band removes a 6 kHz tone");
    }
    {
        // Neutral EQ (no band events) is bypassed -> clean pass-through, unchanged length.
        auto tone = toneTrack (1000.0);
        SessionRenderer::AudioProvider p = [&] (const juce::String&, double& rate) { rate = SR; return &tone; };
        SessionCapture cap; cap.begin (48000.0, 124.0);
        cap.recordDeckLoad (0, { "T", "/t", "", 30, 124, 0, 0, false }, 0.0);
        cap.recordTransport (0, TransportKind::play, 0.0, 0.0);
        cap.recordTransport (0, TransportKind::pause, 0.0, 16.0);
        check (rms (SessionRenderer::render (cap.session(), p)) > 0.2, "untouched EQ is bypassed (tone passes at full level)");
    }

    // ── Mix-bus limiter: ceiling is never exceeded; below-ceiling is transparent ─
    {
        auto peakOf = [] (const juce::AudioBuffer<float>& b)
        {
            float p = 0.0f;
            for (int c = 0; c < b.getNumChannels(); ++c)
                for (int i = 0; i < b.getNumSamples(); ++i) p = juce::jmax (p, std::abs (b.getSample (c, i)));
            return p;
        };

        // A hot mix that would clip: loud source + fader boosted above unity.
        auto hot = sineTrack (SR, 30.0, 300.0);   // 0.5 peak source
        SessionRenderer::AudioProvider p = [&] (const juce::String&, double& rate) { rate = SR; return &hot; };
        SessionCapture cap; cap.begin (48000.0, 124.0);
        cap.recordDeckLoad (0, { "H", "/h", "", 30, 124, 0, 0, false }, 0.0);
        cap.recordParam (0, "fader", 4.0, 0.0);   // +12 dB -> peak ~2.0, must be limited
        cap.recordTransport (0, TransportKind::play, 0.0, 0.0);
        cap.recordTransport (0, TransportKind::pause, 0.0, 16.0);
        const auto s = cap.session();

        const double ceilLin = std::pow (10.0, -1.0 / 20.0);   // -1 dBFS
        const auto limited = SessionRenderer::render (s, p);    // default -1 dB ceiling
        check (allFinite (limited), "limited render is finite");
        check (peakOf (limited) <= (float) ceilLin + 1e-4f, "no sample exceeds the -1 dBFS ceiling");
        check (peakOf (limited) > (float) ceilLin * 0.8f, "limiter drives the mix up near the ceiling");

        // Same session with the limiter disabled would clip well over 1.0.
        SessionRenderer::Options noLim; noLim.limiterCeilingDb = std::numeric_limits<double>::infinity();
        check (peakOf (SessionRenderer::render (s, p, noLim)) > 1.5f, "without the limiter the mix clips (>1.5)");

        // A quiet mix (well under the ceiling) passes through untouched.
        auto quiet = sineTrack (SR, 30.0, 300.0);   // 0.5 peak
        SessionRenderer::AudioProvider pq = [&] (const juce::String&, double& rate) { rate = SR; return &quiet; };
        SessionCapture qc; qc.begin (48000.0, 124.0);
        qc.recordDeckLoad (0, { "Q", "/q", "", 30, 124, 0, 0, false }, 0.0);
        qc.recordParam (0, "fader", 1.0, 0.0);   // peak 0.5, under -1 dB
        qc.recordTransport (0, TransportKind::play, 0.0, 0.0);
        qc.recordTransport (0, TransportKind::pause, 0.0, 16.0);
        const auto withLim = SessionRenderer::render (qc.session(), pq);
        SessionRenderer::Options off; off.limiterCeilingDb = std::numeric_limits<double>::infinity();
        const auto without = SessionRenderer::render (qc.session(), pq, off);
        bool identical = withLim.getNumSamples() == without.getNumSamples();
        for (int i = 0; identical && i < withLim.getNumSamples(); ++i)
            identical = withLim.getSample (0, i) == without.getSample (0, i);
        check (identical, "below-ceiling mix is transparent (limiter changes nothing)");
    }

    std::cout << (fails == 0 ? "RENDERER_OK" : "RENDERER_FAIL (" + std::to_string (fails) + ")") << "\n";
    return fails == 0 ? 0 : 1;
}
