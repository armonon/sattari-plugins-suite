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

namespace stemdeck
{
// Section colour for the arrangement ribbon (kept here so StructureAnalysis.h
// stays graphics-free / unit-testable against juce_audio_basics alone).
juce::Colour sectionColour (const juce::String& label)
{
    if (label == "INTRO")     return juce::Colour (0xff4a90d9);   // blue
    if (label == "BUILD")     return juce::Colour (0xffe6c15a);   // amber
    if (label == "DROP")      return juce::Colour (0xffe6545a);   // hot red
    if (label == "BREAKDOWN") return juce::Colour (0xffb98cff);   // violet
    return juce::Colour (0xff6b7280);                             // OUTRO / grey
}
}

namespace
{
// Draw a min/max waveform of `src`'s first `srcSamples` samples across `area`.
void drawWaveform (juce::Graphics& g, const juce::AudioBuffer<float>& src, juce::int64 srcSamples,
                   juce::Rectangle<float> area, juce::Colour colour)
{
    const int n = (int) juce::jmin ((juce::int64) src.getNumSamples(), srcSamples);
    if (n <= 1 || area.getWidth() < 1.0f) return;
    const float midY = area.getCentreY(), halfH = area.getHeight() * 0.46f;
    const int cols = juce::jmax (1, (int) area.getWidth());
    const auto* d = src.getReadPointer (0);
    g.setColour (colour);
    for (int c = 0; c < cols; ++c)
    {
        const int s0 = (int) ((juce::int64) c * n / cols);
        const int s1 = (int) ((juce::int64) (c + 1) * n / cols);
        float mn = 0.0f, mx = 0.0f;
        for (int i = s0; i < s1; ++i) { mn = juce::jmin (mn, d[i]); mx = juce::jmax (mx, d[i]); }
        const float x = area.getX() + (float) c;
        g.drawVerticalLine ((int) x, midY - mx * halfH, midY - mn * halfH);
    }
}
} // namespace

void ArrangementView::paint (juce::Graphics& g)
{
    const auto bg = juce::Colour (0xff12141a);
    g.fillAll (bg);

    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (kHeaderH);
    auto full = bounds.reduced (10, 6);
    auto cfStrip = full.removeFromBottom (34);   // master crossfader lane at the bottom
    full.removeFromBottom (4);
    auto body = full;

    // Which decks are used?
    std::vector<int> decks;
    for (const auto& e : session.events)
        if (std::find (decks.begin(), decks.end(), e.deck) == decks.end()) decks.push_back (e.deck);
    std::sort (decks.begin(), decks.end());
    if (decks.empty()) decks.push_back (0);

    const double tBeats = totalBeats();
    const double tempo = session.masterTempo > 1.0 ? session.masterTempo : 120.0;

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
            g.setColour (accent.withAlpha (0.16f)); g.fillRoundedRectangle (clip, 3.0f);

            const double clipBeats = endBeat - e.atBeat;
            const double srcSecs = clipBeats * 60.0 / tempo;

            // A structure ribbon (intro/build/drop/breakdown) along the clip top.
            const float ribbonH = 9.0f;
            juce::Rectangle<float> waveArea = clip.reduced (1.0f, 3.0f);
            if (structureProvider)
            {
                const auto sections = structureProvider (e.trackId);
                if (! sections.empty())
                {
                    double trackBeats = 0.0; for (const auto& sec : sections) trackBeats = juce::jmax (trackBeats, sec.endBeat);
                    juce::Graphics::ScopedSaveState ss (g);
                    g.reduceClipRegion (clip.getSmallestIntegerContainer());
                    for (const auto& sec : sections)
                    {
                        // section beats (in the track) -> beats-into-the-clip -> x
                        const float sx0 = clip.getX() + (float) (sec.startBeat / juce::jmax (1.0, trackBeats)) * clip.getWidth();
                        const float sx1 = clip.getX() + (float) (sec.endBeat   / juce::jmax (1.0, trackBeats)) * clip.getWidth();
                        juce::Rectangle<float> band (sx0, clip.getY() + 1.0f, sx1 - sx0, ribbonH);
                        g.setColour (stemdeck::sectionColour (sec.label).withAlpha (0.85f));
                        g.fillRect (band);
                        if (band.getWidth() > 34.0f)
                        {
                            g.setColour (juce::Colours::black.withAlpha (0.8f));
                            g.setFont (juce::FontOptions (7.5f, juce::Font::bold));
                            g.drawText (sec.label, band.reduced (2.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
                        }
                    }
                    waveArea.removeFromTop (ribbonH + 1.0f);
                }
            }

            // Waveform: per-stem in stem colours when stems are available, else mono accent.
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (clip.getSmallestIntegerContainer());
            bool drewStems = false;
            if (stemProvider)
            {
                double srcRate = session.sampleRate;
                const auto stems = stemProvider (e.trackId, srcRate);
                const std::array<juce::Colour, 4> stemCols {
                    juce::Colour (0xffe6a54a),   // drums  amber
                    juce::Colour (0xffb98cff),   // bass   violet
                    juce::Colour (0xff57e39a),   // music  green
                    juce::Colour (0xff45b6e6) }; // vocals blue
                for (int s = 0; s < 4; ++s)
                    if (stems[(size_t) s] != nullptr)
                    { drawWaveform (g, *stems[(size_t) s], (juce::int64) (srcSecs * srcRate), waveArea, stemCols[(size_t) s].withAlpha (0.55f)); drewStems = true; }
            }
            if (! drewStems && audioProvider)
            {
                double srcRate = session.sampleRate;
                if (const auto* buf = audioProvider (e.trackId, srcRate))
                    drawWaveform (g, *buf, (juce::int64) (srcSecs * srcRate), waveArea, accent.withAlpha (0.55f));
            }
            g.setColour (accent.withAlpha (0.9f)); g.drawRoundedRectangle (clip, 3.0f, 1.2f);

            juce::String name = e.trackId;
            for (const auto& t : session.tracks)
                if (t.id == e.trackId) { const auto fn = juce::File (t.path).getFileNameWithoutExtension(); if (fn.isNotEmpty()) name = fn; }
            g.setColour (juce::Colour (0xfff2f5fa));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("DECK " + juce::String::charToString ((juce::juce_wchar) ('A' + deck)) + "  " + name,
                        waveArea.toNearestInt().reduced (6, 2), juce::Justification::topLeft, true);
        }

        // Automation polylines: fader (solid) + low-EQ dB (dashed), mapped over the lane.
        auto automation = [&] (const juce::String& id, double def, double lo, double hi, float thick, bool dashed)
        {
            juce::Path p; bool started = false; double curr = def;
            auto plot = [&] (double beat, double val)
            {
                const float px = beatToX (beat, lane);
                const float f = (float) juce::jlimit (0.0, 1.0, (val - lo) / (hi - lo));
                const float py = (float) lane.getBottom() - 4 - f * (float) (lane.getHeight() - 10);
                if (! started) { p.startNewSubPath (px, py); started = true; } else p.lineTo (px, py);
            };
            for (const auto& e : session.events)
                if (e.deck == deck && e.type == EventType::param && e.paramId == id)
                { plot (e.atBeat, curr); curr = e.value; plot (e.atBeat, curr); }
            if (! started) return;
            plot (tBeats, curr);
            if (dashed) { const float d[2] { 4.0f, 4.0f }; juce::Path dp; juce::PathStrokeType (thick).createDashedStroke (dp, p, d, 2); g.fillPath (dp); }
            else g.strokePath (p, juce::PathStrokeType (thick));
        };
        g.setColour (accent.withAlpha (0.95f));                automation ("fader", 1.0, 0.0, 1.0, 1.6f, false);
        g.setColour (juce::Colour (0xffe6c15a).withAlpha (0.8f)); automation ("low", 0.0, -12.0, 6.0, 1.2f, true);   // EQ low, dashed gold

        y += laneH + kLaneGap;
    }

    // ── Transition objects (drawn ON TOP, spanning the lanes, with handles) ─────
    for (const auto& r : session.regions)
    {
        const float x0 = beatToX (r.startBeat, body), x1 = beatToX (r.endBeat, body);
        juce::Rectangle<float> rr (x0, (float) body.getY() - 2, x1 - x0, (float) body.getHeight() + 4);
        const auto amber = juce::Colour (0xffffb347);   // signature accent for transitions
        g.setColour (amber.withAlpha (0.14f)); g.fillRoundedRectangle (rr, 4.0f);
        g.setColour (amber.withAlpha (0.95f)); g.drawRoundedRectangle (rr, 4.0f, 1.6f);
        // grab handles at each edge
        for (float hx : { rr.getX(), rr.getRight() })
        {
            juce::Rectangle<float> h (hx - 3.0f, rr.getCentreY() - 14.0f, 6.0f, 28.0f);
            g.setColour (amber); g.fillRoundedRectangle (h, 2.0f);
        }
        // label chip
        juce::Rectangle<int> chip ((int) x0 + 6, body.getY() + 3, 150, 15);
        g.setColour (amber.withAlpha (0.9f)); g.fillRoundedRectangle (chip.toFloat(), 3.0f);
        g.setColour (juce::Colour (0xff1a1200));
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (r.kind.toUpperCase() + "  " + juce::String ((int) (r.endBeat - r.startBeat)) + " beats",
                    chip, juce::Justification::centred);
    }

    // ── Master crossfader strip (0=A .. 1=B automation) ─────────────────────────
    {
        g.setColour (juce::Colour (0xff181b22)); g.fillRoundedRectangle (cfStrip.toFloat(), 4.0f);
        g.setColour (juce::Colour (0xff5a6070));
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText ("XFADER  A", cfStrip.getX() + 4, cfStrip.getY() + 1, 60, 10, juce::Justification::topLeft);
        g.drawText ("B", cfStrip.getRight() - 14, cfStrip.getY() + 1, 12, 10, juce::Justification::topLeft);
        juce::Path cf; bool started = false; double curr = 0.5;
        auto plot = [&] (double beat, double v)
        {
            const float px = beatToX (beat, cfStrip);
            const float py = (float) cfStrip.getBottom() - 3 - (float) juce::jlimit (0.0, 1.0, v) * (float) (cfStrip.getHeight() - 12);
            if (! started) { cf.startNewSubPath (px, py); started = true; } else cf.lineTo (px, py);
        };
        for (const auto& e : session.events)
            if (e.type == EventType::param && e.paramId == "crossfader") { plot (e.atBeat, curr); curr = e.value; plot (e.atBeat, curr); }
        plot (tBeats, curr);
        g.setColour (juce::Colour (0xff35c8e6)); g.strokePath (cf, juce::PathStrokeType (1.6f));
    }

    // ── Playhead (spans everything) ─────────────────────────────────────────────
    if (playheadBeat >= 0.0)
    {
        const float x = beatToX (playheadBeat, body);
        g.setColour (juce::Colour (0xffff5a5a));
        g.drawVerticalLine ((int) x, (float) body.getY() - 2, (float) cfStrip.getBottom());
    }
}
