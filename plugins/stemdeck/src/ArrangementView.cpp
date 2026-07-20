#include "ArrangementView.h"
#include "SessionRender.h"

using namespace stemdeck::session;

// ── palette ──────────────────────────────────────────────────────────────────
namespace pal
{
const juce::Colour bgTop   { 0xff0e1017 };
const juce::Colour bgBot   { 0xff08090d };
const juce::Colour panelTop{ 0xff1b1f2a };
const juce::Colour panelBot{ 0xff12151d };
const juce::Colour hair    { 0x14ffffff };
const juce::Colour textHi  { 0xfff3f6fc };
const juce::Colour textMid { 0xff828b9e };
const juce::Colour textLo  { 0xff474e60 };
const juce::Colour accent  { 0xff2fd8f0 };   // product cyan
const juce::Colour amber   { 0xffffb454 };   // transitions
}

namespace stemdeck
{
juce::Colour sectionColour (const juce::String& label)
{
    if (label == "INTRO")     return juce::Colour (0xff4f97e8);
    if (label == "BUILD")     return juce::Colour (0xffe8c25c);
    if (label == "DROP")      return juce::Colour (0xffff5d6c);
    if (label == "BREAKDOWN") return juce::Colour (0xffb98cff);
    return juce::Colour (0xff6b7280);
}
}

namespace
{
juce::Font eyebrow (float h) { juce::Font f (juce::FontOptions (h, juce::Font::bold)); f.setExtraKerningFactor (0.14f); return f; }

void panel (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour top, juce::Colour bot)
{
    g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bot, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (pal::hair);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

// Soft glow around a rounded rect: a few expanding translucent strokes.
void glowRoundedRect (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour c)
{
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (c.withAlpha (0.06f * (float) i));
        g.drawRoundedRectangle (r.expanded ((float) i * 1.6f), radius + i * 1.6f, 1.4f);
    }
}

// Filled mirror waveform with a vertical gradient.
void filledWaveform (juce::Graphics& g, const juce::AudioBuffer<float>& src, juce::int64 srcSamples,
                     juce::Rectangle<float> area, juce::Colour colour)
{
    const int n = (int) juce::jmin ((juce::int64) src.getNumSamples(), srcSamples);
    if (n <= 1 || area.getWidth() < 2.0f) return;
    const float midY = area.getCentreY(), halfH = area.getHeight() * 0.48f;
    const int cols = juce::jmax (2, (int) area.getWidth());
    const auto* d = src.getReadPointer (0);
    juce::Path top, bot;
    top.startNewSubPath (area.getX(), midY);
    for (int c = 0; c < cols; ++c)
    {
        const int s0 = (int) ((juce::int64) c * n / cols), s1 = (int) ((juce::int64) (c + 1) * n / cols);
        float mx = 0.0f; for (int i = s0; i < s1; ++i) mx = juce::jmax (mx, std::abs (d[i]));
        top.lineTo (area.getX() + (float) c, midY - mx * halfH);
    }
    for (int c = cols - 1; c >= 0; --c)
    {
        const int s0 = (int) ((juce::int64) c * n / cols), s1 = (int) ((juce::int64) (c + 1) * n / cols);
        float mx = 0.0f; for (int i = s0; i < s1; ++i) mx = juce::jmax (mx, std::abs (d[i]));
        top.lineTo (area.getX() + (float) c, midY + mx * halfH);
    }
    top.closeSubPath();
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.85f), area.getX(), area.getY(),
                                             colour.withAlpha (0.30f), area.getX(), area.getBottom(), false));
    g.fillPath (top);
}
} // namespace

double ArrangementView::totalBeats() const
{
    double last = 0.0;
    for (const auto& e : session.events) last = juce::jmax (last, e.atBeat);
    for (const auto& r : session.regions) last = juce::jmax (last, r.endBeat);
    return juce::jmax (16.0, last + 8.0);
}

float ArrangementView::beatToX (double beat, juce::Rectangle<int> area) const
{
    return (float) area.getX() + (float) (beat / totalBeats()) * (float) area.getWidth();
}

juce::Colour ArrangementView::deckColour (int deck)
{
    static const juce::Colour c[4] { juce::Colour (0xff2fd8f0), juce::Colour (0xff4ee6a8),
                                     juce::Colour (0xffb98cff), juce::Colour (0xffe8c25c) };
    return c[juce::jlimit (0, 3, deck)];
}

void ArrangementView::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    // Background: deep gradient + a faint top glow.
    g.setGradientFill (juce::ColourGradient (pal::bgTop, 0, 0, pal::bgBot, 0, full.getHeight(), false));
    g.fillAll (pal::bgTop);
    g.fillRect (full);
    g.setGradientFill (juce::ColourGradient (pal::accent.withAlpha (0.06f), full.getCentreX(), -40.0f,
                                             juce::Colours::transparentBlack, full.getCentreX(), 120.0f, false));
    g.fillRect (full.removeFromTop (140.0f));

    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (34);
    auto ruler  = bounds.removeFromTop (16);
    auto area   = bounds.reduced (14, 8);
    auto cfStrip = area.removeFromBottom (30); area.removeFromBottom (8);
    auto body = area;

    const double tBeats = totalBeats();
    const double tempo = session.masterTempo > 1.0 ? session.masterTempo : 120.0;

    std::vector<int> decks;
    for (const auto& e : session.events)
        if (std::find (decks.begin(), decks.end(), e.deck) == decks.end()) decks.push_back (e.deck);
    std::sort (decks.begin(), decks.end());
    if (decks.empty()) decks.push_back (0);

    // ── Header ─────────────────────────────────────────────────────────────────
    {
        auto h = header.reduced (14, 0);
        g.setColour (pal::textMid);
        g.setFont (eyebrow (11.0f));
        g.drawText ("ARRANGEMENT", h.removeFromLeft (140), juce::Justification::centredLeft);

        // Right cluster, laid out right-to-left with breathing room.
        auto bpmNum = h.removeFromRight (48);
        g.setColour (pal::accent);
        g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
        g.drawText (juce::String (tempo, 0), bpmNum, juce::Justification::centredRight);

        auto bpmLbl = h.removeFromRight (34);
        g.setColour (pal::textLo); g.setFont (eyebrow (8.0f));
        g.drawText ("BPM", bpmLbl, juce::Justification::centredRight);

        h.removeFromRight (18);
        g.setColour (pal::textMid); g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String ((int) (tBeats / 4)) + " bars    " + juce::String (session.events.size()) + " events",
                    h, juce::Justification::centredRight);
    }

    // ── Time ruler ─────────────────────────────────────────────────────────────
    for (int beat = 0; beat <= (int) tBeats; beat += 4)
    {
        const float x = beatToX (beat, body);
        const bool bar4 = (beat % 16) == 0;
        g.setColour (bar4 ? pal::textLo : pal::hair);
        g.drawVerticalLine ((int) x, (float) ruler.getBottom() - (bar4 ? 8.0f : 4.0f), (float) ruler.getBottom());
        if (bar4)
        {
            g.setColour (pal::textLo); g.setFont (juce::FontOptions (8.5f));
            g.drawText (juce::String (beat / 4 + 1), (int) x + 3, ruler.getY(), 30, ruler.getHeight(), juce::Justification::centredLeft);
        }
        // faint grid down the body
        g.setColour (bar4 ? juce::Colour (0x0cffffff) : juce::Colour (0x06ffffff));
        g.drawVerticalLine ((int) x, (float) body.getY(), (float) cfStrip.getBottom());
    }

    // ── Lanes ──────────────────────────────────────────────────────────────────
    const int laneH = (body.getHeight() - 8 * ((int) decks.size() - 1)) / (int) decks.size();
    int y = body.getY();
    for (int deck : decks)
    {
        juce::Rectangle<int> lane (body.getX(), y, body.getWidth(), laneH);
        const auto accent = deckColour (deck);
        panel (g, lane.toFloat(), 6.0f, pal::panelTop, pal::panelBot);

        for (const auto& e : session.events)
        {
            if (e.deck != deck || e.type != EventType::deckLoad) continue;
            double endBeat = tBeats;
            for (const auto& e2 : session.events)
                if (e2.deck == deck && e2.type == EventType::transport && e2.transport == TransportKind::pause && e2.atBeat > e.atBeat)
                { endBeat = e2.atBeat; break; }

            const float cx0 = beatToX (e.atBeat, lane), cx1 = beatToX (endBeat, lane);
            juce::Rectangle<float> clip (cx0, (float) lane.getY() + 4, cx1 - cx0, (float) lane.getHeight() - 8);
            // clip body: subtle accent-tinted gradient
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.20f), clip.getX(), clip.getY(),
                                                     accent.withAlpha (0.06f), clip.getX(), clip.getBottom(), false));
            g.fillRoundedRectangle (clip, 4.0f);

            const double srcSecs = (endBeat - e.atBeat) * 60.0 / tempo;
            juce::Rectangle<float> waveArea = clip.reduced (2.0f, 4.0f);

            // structure ribbon
            if (structureProvider)
            {
                const auto sections = structureProvider (e.trackId);
                if (! sections.empty())
                {
                    double trackBeats = 0; for (const auto& s : sections) trackBeats = juce::jmax (trackBeats, s.endBeat);
                    juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (clip.getSmallestIntegerContainer());
                    for (const auto& s : sections)
                    {
                        const float sx0 = clip.getX() + (float) (s.startBeat / juce::jmax (1.0, trackBeats)) * clip.getWidth();
                        const float sx1 = clip.getX() + (float) (s.endBeat   / juce::jmax (1.0, trackBeats)) * clip.getWidth();
                        juce::Rectangle<float> band (sx0 + 1, clip.getY() + 2, sx1 - sx0 - 2, 11.0f);
                        const auto sc = stemdeck::sectionColour (s.label);
                        g.setGradientFill (juce::ColourGradient (sc.withAlpha (0.95f), band.getX(), band.getY(),
                                                                 sc.withAlpha (0.55f), band.getX(), band.getBottom(), false));
                        g.fillRoundedRectangle (band, 2.0f);
                        if (band.getWidth() > 40.0f)
                        {
                            g.setColour (juce::Colours::black.withAlpha (0.75f)); g.setFont (eyebrow (7.5f));
                            g.drawText (s.label, band.reduced (4, 0).toNearestInt(), juce::Justification::centredLeft);
                        }
                    }
                    waveArea.removeFromTop (13.0f);
                }
            }

            // stem-coloured filled waveforms (or mono accent fallback)
            juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (clip.getSmallestIntegerContainer());
            bool drew = false;
            if (stemProvider)
            {
                double rate = session.sampleRate; const auto stems = stemProvider (e.trackId, rate);
                const std::array<juce::Colour, 4> sc { juce::Colour (0xffff8a4c), juce::Colour (0xffb47cff),
                                                       juce::Colour (0xff4ee6a8), juce::Colour (0xff45c8ff) };
                for (int s = 0; s < 4; ++s) if (stems[(size_t) s]) { filledWaveform (g, *stems[(size_t) s], (juce::int64) (srcSecs * rate), waveArea, sc[(size_t) s]); drew = true; }
            }
            if (! drew && audioProvider)
            { double rate = session.sampleRate; if (const auto* buf = audioProvider (e.trackId, rate)) filledWaveform (g, *buf, (juce::int64) (srcSecs * rate), waveArea, accent); }

            g.setColour (accent.withAlpha (0.85f)); g.drawRoundedRectangle (clip, 4.0f, 1.3f);

            // deck chip
            juce::String name = e.trackId;
            for (const auto& t : session.tracks) if (t.id == e.trackId) { const auto fn = juce::File (t.path).getFileNameWithoutExtension(); if (fn.isNotEmpty()) name = fn; }
            auto chip = juce::Rectangle<float> (clip.getX() + 5, waveArea.getY() + 3, juce::jmin (clip.getWidth() - 10, 240.0f), 15.0f);
            g.setColour (juce::Colours::black.withAlpha (0.42f)); g.fillRoundedRectangle (chip, 3.0f);
            g.setColour (accent); g.fillRoundedRectangle (chip.removeFromLeft (4).withTrimmedTop (2).withTrimmedBottom (2), 1.5f);
            g.setColour (pal::textHi); g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + deck)) + "  " + name,
                        chip.reduced (6, 0).toNearestInt(), juce::Justification::centredLeft, true);
        }

        // automation: fader (glow line) + EQ-low (dashed gold)
        auto autom = [&] (const juce::String& id, double def, double lo, double hi, juce::Colour col, bool dashed)
        {
            juce::Path p; bool started = false; double curr = def;
            auto plot = [&] (double beat, double v) {
                const float px = beatToX (beat, lane);
                const float f = (float) juce::jlimit (0.0, 1.0, (v - lo) / (hi - lo));
                const float py = (float) lane.getBottom() - 5 - f * (float) (lane.getHeight() - 12);
                if (! started) { p.startNewSubPath (px, py); started = true; } else p.lineTo (px, py);
            };
            for (const auto& e : session.events) if (e.deck == deck && e.type == EventType::param && e.paramId == id) { plot (e.atBeat, curr); curr = e.value; plot (e.atBeat, curr); }
            if (! started) return; plot (tBeats, curr);
            if (dashed) { const float d[2] { 4, 4 }; juce::Path dp; juce::PathStrokeType (1.2f).createDashedStroke (dp, p, d, 2); g.setColour (col.withAlpha (0.8f)); g.fillPath (dp); }
            else { g.setColour (col.withAlpha (0.25f)); g.strokePath (p, juce::PathStrokeType (3.2f)); g.setColour (col); g.strokePath (p, juce::PathStrokeType (1.5f)); }
        };
        autom ("fader", 1.0, 0.0, 1.0, accent, false);
        autom ("low", 0.0, -12.0, 6.0, juce::Colour (0xffe8c25c), true);

        y += laneH + 8;
    }

    // ── Transition objects (glowing, on top) ────────────────────────────────────
    for (const auto& r : session.regions)
    {
        const float x0 = beatToX (r.startBeat, body), x1 = beatToX (r.endBeat, body);
        juce::Rectangle<float> rr (x0, (float) body.getY() - 3, x1 - x0, (float) body.getHeight() + 6);
        glowRoundedRect (g, rr, 6.0f, pal::amber);
        g.setGradientFill (juce::ColourGradient (pal::amber.withAlpha (0.18f), rr.getX(), rr.getY(),
                                                 pal::amber.withAlpha (0.05f), rr.getX(), rr.getBottom(), false));
        g.fillRoundedRectangle (rr, 6.0f);
        g.setColour (pal::amber.withAlpha (0.95f)); g.drawRoundedRectangle (rr, 6.0f, 1.6f);
        for (float hx : { rr.getX(), rr.getRight() })
        { juce::Rectangle<float> h (hx - 3.0f, rr.getCentreY() - 16.0f, 6.0f, 32.0f); g.setColour (pal::amber); g.fillRoundedRectangle (h, 3.0f); }
        auto chip = juce::Rectangle<float> (x0 + 8, (float) body.getY() + 4, 160, 16);
        g.setColour (pal::amber); g.fillRoundedRectangle (chip, 4.0f);
        g.setColour (juce::Colour (0xff1a1200)); g.setFont (eyebrow (9.0f));
        g.drawText ("TRANSITION  " + juce::String ((int) (r.endBeat - r.startBeat)) + " BEATS", chip.toNearestInt(), juce::Justification::centred);
    }

    // ── Crossfader strip ────────────────────────────────────────────────────────
    {
        panel (g, cfStrip.toFloat(), 5.0f, pal::panelTop, pal::panelBot);
        g.setColour (pal::textLo); g.setFont (eyebrow (8.0f));
        g.drawText ("XFADER", cfStrip.getX() + 6, cfStrip.getY(), 60, cfStrip.getHeight(), juce::Justification::centredLeft);
        g.drawText ("A", cfStrip.getX() + 52, cfStrip.getY(), 12, cfStrip.getHeight(), juce::Justification::centred);
        g.drawText ("B", cfStrip.getRight() - 16, cfStrip.getY(), 12, cfStrip.getHeight(), juce::Justification::centred);
        juce::Path cf; bool started = false; double curr = 0.5;
        auto plot = [&] (double beat, double v) {
            const float px = beatToX (beat, cfStrip);
            const float py = (float) cfStrip.getBottom() - 4 - (float) juce::jlimit (0.0, 1.0, v) * (float) (cfStrip.getHeight() - 10);
            if (! started) { cf.startNewSubPath (px, py); started = true; } else cf.lineTo (px, py);
        };
        for (const auto& e : session.events) if (e.type == EventType::param && e.paramId == "crossfader") { plot (e.atBeat, curr); curr = e.value; plot (e.atBeat, curr); }
        plot (tBeats, curr);
        g.setColour (pal::accent.withAlpha (0.25f)); g.strokePath (cf, juce::PathStrokeType (3.0f));
        g.setColour (pal::accent); g.strokePath (cf, juce::PathStrokeType (1.5f));
    }

    // ── Playhead (glow + head) ──────────────────────────────────────────────────
    if (playheadBeat >= 0.0)
    {
        const float x = beatToX (playheadBeat, body);
        const auto ph = juce::Colour (0xffff5566);
        g.setColour (ph.withAlpha (0.18f)); g.fillRect (juce::Rectangle<float> (x - 2.5f, (float) body.getY() - 3, 5.0f, (float) cfStrip.getBottom() - body.getY() + 3));
        g.setColour (ph); g.drawVerticalLine ((int) x, (float) body.getY() - 3, (float) cfStrip.getBottom());
        juce::Path head; head.addTriangle (x - 5, (float) body.getY() - 8, x + 5, (float) body.getY() - 8, x, (float) body.getY() - 1);
        g.fillPath (head);
    }
}
