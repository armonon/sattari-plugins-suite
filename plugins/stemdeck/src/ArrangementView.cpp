#include "ArrangementView.h"
#include "SessionRender.h"

using namespace stemdeck::session;

double ArrangementView::totalBeats() const
{
    double last = 0.0;
    for (const auto& e : session.events) last = juce::jmax (last, e.atBeat);
    for (const auto& r : session.regions) last = juce::jmax (last, r.endBeat);
    return juce::jmax (16.0, last + 8.0);   // a little tail
}

float ArrangementView::beatToX (double beat, juce::Rectangle<int> area) const
{
    const double t = totalBeats();
    return (float) area.getX() + (float) (beat / t) * (float) area.getWidth();
}

juce::Colour ArrangementView::deckColour (int deck)
{
    static const juce::Colour cols[4] { juce::Colour (0xff35c8e6),   // cyan
                                        juce::Colour (0xff57e39a),   // mint
                                        juce::Colour (0xffb98cff),   // violet
                                        juce::Colour (0xffe6c15a) }; // gold
    return cols[juce::jlimit (0, 3, deck)];
}

void ArrangementView::paint (juce::Graphics& g)
{
    const auto bg = juce::Colour (0xff12141a);
    g.fillAll (bg);

    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (kHeaderH);
    auto body = bounds.reduced (10, 6);

    // Which decks are used?
    std::vector<int> decks;
    for (const auto& e : session.events)
        if (std::find (decks.begin(), decks.end(), e.deck) == decks.end()) decks.push_back (e.deck);
    std::sort (decks.begin(), decks.end());
    if (decks.empty()) decks.push_back (0);

    const double tBeats = totalBeats();

    // ── Header: title + tempo ──────────────────────────────────────────────────
    g.setColour (juce::Colour (0xffe8ecf2));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("ARRANGEMENT", header.reduced (12, 0), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff8b93a3));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String (session.masterTempo, 1) + " BPM   "
                + juce::String ((int) tBeats) + " beats   "
                + juce::String (session.events.size()) + " events",
                header.reduced (12, 0), juce::Justification::centredRight);

    // ── Beat/bar grid (every 4 beats) ──────────────────────────────────────────
    for (int beat = 0; beat <= (int) tBeats; beat += 4)
    {
        const float x = beatToX (beat, body);
        const bool bar = (beat % 16) == 0;
        g.setColour (juce::Colour (bar ? 0x22ffffff : 0x11ffffff));
        g.drawVerticalLine ((int) x, (float) body.getY(), (float) body.getBottom());
        if (bar)
        {
            g.setColour (juce::Colour (0xff5a6070));
            g.setFont (juce::FontOptions (9.0f));
            g.drawText (juce::String (beat / 4 + 1), (int) x + 2, body.getY() - 1, 30, 10, juce::Justification::topLeft);
        }
    }

    // ── Transition regions (behind the lanes) ──────────────────────────────────
    for (const auto& r : session.regions)
    {
        const float x0 = beatToX (r.startBeat, body), x1 = beatToX (r.endBeat, body);
        juce::Rectangle<float> rr ((float) x0, (float) body.getY(), x1 - x0, (float) body.getHeight());
        g.setColour (juce::Colour (0x2235c8e6));
        g.fillRect (rr);
        g.setColour (juce::Colour (0x8835c8e6));
        g.drawRect (rr, 1.0f);
        g.setColour (juce::Colour (0xffbfe9f5));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (r.kind.toUpperCase(), (int) x0 + 3, body.getY() + 2, 120, 12, juce::Justification::topLeft);
    }

    // ── Per-deck lanes ─────────────────────────────────────────────────────────
    const int laneH = (body.getHeight() - kLaneGap * ((int) decks.size() - 1)) / (int) decks.size();
    int y = body.getY();
    for (int deck : decks)
    {
        juce::Rectangle<int> lane (body.getX(), y, body.getWidth(), laneH);
        const auto accent = deckColour (deck);

        g.setColour (juce::Colour (0xff181b22));
        g.fillRoundedRectangle (lane.toFloat(), 4.0f);

        // Track clips: from each deckLoad to the next pause/EOF on this deck.
        for (const auto& e : session.events)
        {
            if (e.deck != deck || e.type != EventType::deckLoad) continue;
            double endBeat = tBeats;
            for (const auto& e2 : session.events)
                if (e2.deck == deck && e2.type == EventType::transport
                    && e2.transport == TransportKind::pause && e2.atBeat > e.atBeat)
                { endBeat = e2.atBeat; break; }

            const float cx0 = beatToX (e.atBeat, lane), cx1 = beatToX (endBeat, lane);
            juce::Rectangle<float> clip (cx0, (float) lane.getY() + 3, cx1 - cx0, (float) lane.getHeight() - 6);
            g.setColour (accent.withAlpha (0.22f)); g.fillRoundedRectangle (clip, 3.0f);
            g.setColour (accent.withAlpha (0.9f));  g.drawRoundedRectangle (clip, 3.0f, 1.2f);

            juce::String name = e.trackId;
            for (const auto& t : session.tracks)
                if (t.id == e.trackId) { const auto fn = juce::File (t.path).getFileNameWithoutExtension(); if (fn.isNotEmpty()) name = fn; }
            g.setColour (juce::Colour (0xffe8ecf2));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("DECK " + juce::String::charToString ((juce::juce_wchar) ('A' + deck)) + "  " + name,
                        clip.toNearestInt().reduced (6, 3), juce::Justification::topLeft, true);
        }

        // Fader automation polyline (0..1 over the lane).
        juce::Path fader;
        bool started = false;
        double curr = 1.0;
        auto plot = [&] (double beat, double val)
        {
            const float px = beatToX (beat, lane);
            const float py = (float) lane.getBottom() - 3 - (float) juce::jlimit (0.0, 1.0, val) * (float) (lane.getHeight() - 8);
            if (! started) { fader.startNewSubPath (px, py); started = true; } else fader.lineTo (px, py);
        };
        for (const auto& e : session.events)
            if (e.deck == deck && e.type == EventType::param && e.paramId == "fader")
            { plot (e.atBeat, curr); curr = e.value; plot (e.atBeat, curr); }
        plot (tBeats, curr);
        g.setColour (accent.withAlpha (0.8f));
        g.strokePath (fader, juce::PathStrokeType (1.4f));

        y += laneH + kLaneGap;
    }

    // ── Playhead ───────────────────────────────────────────────────────────────
    if (playheadBeat >= 0.0)
    {
        const float x = beatToX (playheadBeat, body);
        g.setColour (juce::Colour (0xffff5a5a));
        g.drawVerticalLine ((int) x, (float) body.getY(), (float) body.getBottom());
    }
}
