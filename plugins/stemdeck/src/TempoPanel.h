#pragma once

// TempoPanel — the unified tempo/BPM panel (perform-screen §5). One panel shows both
// ground truth and live tempo at once, plus the drawn tempo map and the retroactive
// tempo lane, instead of djay's flip between Edit-BPM and Tempo modes.
//
//   5.1 Track BPM   — ground truth: big readout + detection source & confidence,
//                     octave-error correction (÷2 ×2), Dynamic vs Straight.
//   5.2 Playing at  — live tempo + % deviation + pitch-fader range + key/formant lock.
//   5.3/5.4 Graph   — the tempo drawn as a curve across the track (the map), with the
//                     editable retroactive lane overlaid and the playhead marked.
//
// Header-only for standalone PNG verification. Drawing only; live feeds + editable
// drag points are wired by the host.

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <algorithm>
#include "DesignTokens.h"

namespace stemdeck
{

struct TempoPanelModel
{
    double trackBpm   = 124.0;     // ground truth
    double liveBpm    = 124.0;     // playing at
    double nativeBpm  = 124.0;     // deck's native (for the +N readout)
    float  confidence = 1.0f;      // 0..1
    juce::String source = "neural"; // neural / DSP / manual / host
    bool   dynamic    = false;     // Dynamic (follow map) vs Straight (fixed)
    int    pitchRange = 8;         // +/-8 / 16 / 50 %

    // Tempo map (detected drift) + retroactive lane, both as (beatNorm 0..1, bpm).
    std::vector<std::pair<float, double>> map;
    std::vector<std::pair<float, double>> lane;
    float  playheadNorm = 0.0f;
};

// c < 0 means "confidence unknown" (the analysis doesn't emit one) — that is NOT
// treated as low confidence, so we never fabricate a warning.
// c < 0 means "confidence unknown" — see below.
inline bool tempoLowConfidence (float c) { return c >= 0.0f && c < 0.65f; }

// Graph rect + BPM axis (lo..lo+span) for the tempo map/lane. Shared by draw + hit-
// test so clicks land where the curve is drawn.
inline void tempoGraphGeometry (juce::Rectangle<float> area, const TempoPanelModel& m,
                                juce::Rectangle<float>& graph, double& lo, double& span)
{
    auto b = area.reduced (12.0f, 10.0f);
    b.removeFromLeft (150.0f); b.removeFromLeft (10.0f);
    b.removeFromLeft (150.0f); b.removeFromLeft (10.0f);
    b.removeFromTop (12.0f);   // header row
    graph = b;

    double hi = m.trackBpm; lo = m.trackBpm;
    for (auto& p : m.map)  { lo = juce::jmin (lo, p.second); hi = juce::jmax (hi, p.second); }
    for (auto& p : m.lane) { lo = juce::jmin (lo, p.second); hi = juce::jmax (hi, p.second); }
    lo -= 2.0; hi += 2.0;
    span = juce::jmax (1.0, hi - lo);
}

inline void drawTempoPanel (juce::Graphics& g, juce::Rectangle<float> area, const TempoPanelModel& m)
{
    g.setColour (stemdeck::tokens::c (stemdeck::tokens::bgPanel2));
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (stemdeck::tokens::c (stemdeck::tokens::borderSubtle));
    g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

    auto b = area.reduced (12.0f, 10.0f);
    const bool lowConf = tempoLowConfidence (m.confidence);
    const auto amber = stemdeck::tokens::c (stemdeck::tokens::warn2);

    // ── 5.1 Track BPM (left) ────────────────────────────────────────────────────
    auto left = b.removeFromLeft (150.0f);
    {
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgTertiary));
        g.setFont (juce::FontOptions (8.0f, juce::Font::bold).withKerningFactor (0.08f));
        g.drawText ("TRACK BPM", left.removeFromTop (12.0f).toNearestInt(), juce::Justification::centredLeft);

        auto row = left.removeFromTop (34.0f);
        g.setColour (lowConf ? amber : stemdeck::tokens::c (stemdeck::tokens::fgPrimary));
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawText (juce::String (m.trackBpm, 1), row, juce::Justification::centredLeft);

        // source + confidence (italic + faded when low, per the AI-honesty rule).
        auto conf = left.removeFromTop (14.0f);
        juce::Font cf (juce::FontOptions (10.0f)); cf.setItalic (lowConf);
        g.setColour ((lowConf ? amber : stemdeck::tokens::c (stemdeck::tokens::fgSecondary)).withAlpha (lowConf ? 0.8f : 1.0f));
        g.setFont (cf);
        g.drawText (m.confidence < 0.0f ? m.source : m.source + "  " + juce::String (m.confidence, 2),
                    conf, juce::Justification::centredLeft);

        // low-confidence octave-error nudge (§5.1).
        if (lowConf)
        {
            g.setColour (amber);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText ("might be " + juce::String (m.trackBpm * 2.0, 0) + "?  x2",
                        left.removeFromTop (13.0f), juce::Justification::centredLeft);
        }

        // ÷2 · ×2 · edit · tap
        auto tools = left.removeFromTop (18.0f);
        const char* lab[] = { "/2", "x2", "edit", "tap" };
        const float tw = tools.getWidth() / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            auto cell = tools.removeFromLeft (tw).reduced (1.0f, 0.0f);
            g.setColour (stemdeck::tokens::c (stemdeck::tokens::bgControl)); g.fillRoundedRectangle (cell, 3.0f);
            g.setColour (i == 1 && lowConf ? amber : stemdeck::tokens::c (stemdeck::tokens::fgSecondary));   // ×2 pushed forward when low-conf
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (lab[i], cell, juce::Justification::centred);
        }

        // Dynamic / Straight
        auto ds = left.removeFromTop (18.0f).withTrimmedTop (3.0f);
        auto half = ds.withWidth (ds.getWidth() / 2.0f);
        auto seg = [&] (juce::Rectangle<float> h, const juce::String& t, bool on)
        {
            if (on) { g.setColour (stemdeck::tokens::c (stemdeck::tokens::deckA).withAlpha (0.22f)); g.fillRoundedRectangle (h.reduced (1), 3.0f); }
            g.setColour (on ? stemdeck::tokens::c (stemdeck::tokens::deckA) : stemdeck::tokens::c (stemdeck::tokens::fgTertiary));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (t, h, juce::Justification::centred);
        };
        seg (half, "Dynamic", m.dynamic);
        seg (half.withX (half.getRight()), "Straight", ! m.dynamic);
    }

    b.removeFromLeft (10.0f);

    // ── 5.2 Playing at (middle) ────────────────────────────────────────────────
    auto mid = b.removeFromLeft (150.0f);
    {
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgTertiary));
        g.setFont (juce::FontOptions (8.0f, juce::Font::bold).withKerningFactor (0.08f));
        g.drawText ("PLAYING AT", mid.removeFromTop (12.0f).toNearestInt(), juce::Justification::centredLeft);

        auto row = mid.removeFromTop (34.0f);
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgPrimary));
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawText (juce::String (m.liveBpm, 1), row, juce::Justification::centredLeft);

        const double pct = m.nativeBpm > 0 ? (m.liveBpm / m.nativeBpm - 1.0) * 100.0 : 0.0;
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgSecondary));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (juce::String::formatted ("native %.1f -> %+.1f  (%+.1f%%)",
                                             m.nativeBpm, m.liveBpm - m.nativeBpm, pct),
                    mid.removeFromTop (14.0f), juce::Justification::centredLeft);

        // pitch range +/- and key/formant lock.
        auto pr = mid.removeFromTop (18.0f).withTrimmedTop (2.0f);
        const int ranges[] = { 8, 16, 50 };
        const float rw = pr.getWidth() / 3.0f;
        for (int i = 0; i < 3; ++i)
        {
            auto cell = pr.removeFromLeft (rw).reduced (1.0f, 0.0f);
            const bool on = m.pitchRange == ranges[i];
            g.setColour (on ? stemdeck::tokens::c (stemdeck::tokens::borderSubtle) : stemdeck::tokens::c (stemdeck::tokens::bgControl)); g.fillRoundedRectangle (cell, 3.0f);
            g.setColour (on ? stemdeck::tokens::c (stemdeck::tokens::fgPrimary) : stemdeck::tokens::c (stemdeck::tokens::fgTertiary));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText ("+/-" + juce::String (ranges[i]), cell, juce::Justification::centred);
        }
        auto kf = mid.removeFromTop (18.0f).withTrimmedTop (3.0f);
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::deckA).withAlpha (0.22f)); g.fillRoundedRectangle (kf, 3.0f);
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::deckA));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText ("key + formant lock", kf, juce::Justification::centred);
    }

    b.removeFromLeft (10.0f);

    // ── 5.3/5.4 Tempo map + retroactive lane (right) ───────────────────────────
    {
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgTertiary));
        g.setFont (juce::FontOptions (8.0f, juce::Font::bold).withKerningFactor (0.08f));
        g.drawText ("TEMPO MAP   RETROACTIVE LANE", b.removeFromTop (12.0f).toNearestInt(), juce::Justification::centredLeft);

        juce::Rectangle<float> graph; double lo = 0.0, span = 1.0;
        tempoGraphGeometry (area, m, graph, lo, span);
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::bgApp));
        g.fillRoundedRectangle (graph, 4.0f);

        auto yFor = [&] (double bpm) { return graph.getBottom() - (float) ((bpm - lo) / span) * graph.getHeight(); };
        auto xFor = [&] (float nrm)  { return graph.getX() + nrm * graph.getWidth(); };

        // Unity (performed base tempo) reference line.
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::borderSubtle));
        const float uy = yFor (m.trackBpm);
        g.drawHorizontalLine ((int) uy, graph.getX(), graph.getRight());

        // Detected drift curve (the map).
        auto poly = [&] (const std::vector<std::pair<float, double>>& pts, juce::Colour c, float th)
        {
            if (pts.size() < 2) return;
            juce::Path path;
            path.startNewSubPath (xFor (pts.front().first), yFor (pts.front().second));
            for (size_t i = 1; i < pts.size(); ++i) path.lineTo (xFor (pts[i].first), yFor (pts[i].second));
            g.setColour (c);
            g.strokePath (path, juce::PathStrokeType (th));
        };
        poly (m.map, stemdeck::tokens::c (stemdeck::tokens::fgMuted), 1.2f);                 // drift = grey
        // Reference §7: the retroactive lane is VIOLET, not the live accent. It means
        // something different from every other number on this panel — "perform at 124,
        // ride to 128, re-times on export" — so it must not read as live tempo.
        poly (m.lane, stemdeck::tokens::c (stemdeck::tokens::deckC), 2.0f);

        // Editable lane points.
        for (auto& p : m.lane)
        {
            g.setColour (stemdeck::tokens::c (stemdeck::tokens::bgApp)); g.fillEllipse (xFor (p.first) - 4, yFor (p.second) - 4, 8, 8);
            g.setColour (stemdeck::tokens::c (stemdeck::tokens::deckA)); g.fillEllipse (xFor (p.first) - 3, yFor (p.second) - 3, 6, 6);
        }

        // Playhead.
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::warn2));
        g.fillRect (xFor (m.playheadNorm) - 0.5f, graph.getY(), 1.0f, graph.getHeight());
    }
}

// Self-painting panel (host sets the model + repaints).
class TempoPanel : public juce::Component
{
public:
    TempoPanel() = default;
    void setModel (TempoPanelModel m) { model = std::move (m); repaint(); }
    const TempoPanelModel& getModel() const { return model; }
    void paint (juce::Graphics& g) override { drawTempoPanel (g, getLocalBounds().toFloat(), model); }

    // Fired whenever the retroactive lane is edited (normalised (beat, bpm) points).
    std::function<void (std::vector<std::pair<float, double>>)> onLaneChanged;

    void mouseDown (const juce::MouseEvent& e) override
    {
        juce::Rectangle<float> graph; double lo, span;
        tempoGraphGeometry (getLocalBounds().toFloat(), model, graph, lo, span);
        if (! graph.contains (e.position)) { dragIdx = -1; return; }

        // Grab the nearest existing point, else drop a new one at the click.
        dragIdx = -1; float best = 14.0f;
        for (int i = 0; i < (int) model.lane.size(); ++i)
        {
            const float px = graph.getX() + model.lane[(size_t) i].first * graph.getWidth();
            const float py = graph.getBottom() - (float) ((model.lane[(size_t) i].second - lo) / span) * graph.getHeight();
            const float d = e.position.getDistanceFrom ({ px, py });
            if (d < best) { best = d; dragIdx = i; }
        }
        if (dragIdx < 0)
        {
            const float nrm = juce::jlimit (0.0f, 1.0f, (e.position.x - graph.getX()) / graph.getWidth());
            model.lane.push_back ({ nrm, bpmAtY (e.position.y, graph, lo, span) });
            std::sort (model.lane.begin(), model.lane.end(),
                       [] (auto& a, auto& b) { return a.first < b.first; });
            for (int i = 0; i < (int) model.lane.size(); ++i) if (std::abs (model.lane[(size_t) i].first - nrm) < 1e-4f) dragIdx = i;
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragIdx < 0 || dragIdx >= (int) model.lane.size()) return;
        juce::Rectangle<float> graph; double lo, span;
        tempoGraphGeometry (getLocalBounds().toFloat(), model, graph, lo, span);
        model.lane[(size_t) dragIdx].second = bpmAtY (e.position.y, graph, lo, span);   // y => tempo
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragIdx >= 0 && onLaneChanged) onLaneChanged (model.lane);
        dragIdx = -1;
    }

private:
    static double bpmAtY (float y, juce::Rectangle<float> graph, double lo, double span)
    {
        const double t = juce::jlimit (0.0, 1.0, (double) (graph.getBottom() - y) / graph.getHeight());
        return lo + t * span;
    }

    TempoPanelModel model;
    int dragIdx = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TempoPanel)
};

} // namespace stemdeck
