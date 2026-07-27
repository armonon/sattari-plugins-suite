#pragma once

// WaveformRegion — the focused deck's two-zone waveform (perform-screen §2).
//
//   Overview (top): the whole track as one horizontal waveform that never scrolls,
//   with structure regions shaded/labelled, a window box marking the detail slice,
//   and the playhead.
//
//   Detail (below): four SIDE-BY-SIDE VERTICAL stem columns (vox/drum/bass/other)
//   that scroll downward as the track plays, with a fixed horizontal "now" line, a
//   bar-number gutter, a beat grid, and always-visible stem labels. A muted stem's
//   column dims. A stems<->freq toggle flips the columns between per-stem colour and
//   a single amplitude waveform.
//
// The render path is isolated in drawStemColumn / drawOverview so it can later be
// swapped to a GPU peak-texture/shader path without touching layout or interaction.
// This CPU version is the structural first increment; live peak plumbing + GPU are
// the multi-week follow-on.

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <array>
#include "DesignTokens.h"

namespace stemdeck
{

class WaveformRegion : public juce::Component,
                       public juce::FileDragAndDropTarget
{
public:
    enum class ColourMode { stems, frequency };
    static constexpr int kNumStems = 4;   // vox, drum, bass, other

    struct StructureRegion { float startNorm = 0.0f, endNorm = 0.0f; juce::String label; juce::Colour colour; };

    WaveformRegion()
    {
        stemColour = { stemdeck::tokens::c (stemdeck::tokens::stemVox),   // VOX rose
                       stemdeck::tokens::c (stemdeck::tokens::deckB),   // DRM blue
                       stemdeck::tokens::c (stemdeck::tokens::deckA),   // BAS teal
                       stemdeck::tokens::c (stemdeck::tokens::stemOth) };  // OTH neutral
        stemLabel  = { "VOX", "DRM", "BAS", "OTH" };
    }

    // ── Data (abs peaks 0..1 across the WHOLE track; same length per stem) ────────
    void setOverviewPeaks (std::vector<float> p) { overview = std::move (p); repaint(); }
    void setStemPeaks (int stem, std::vector<float> p)
    {
        if (juce::isPositiveAndBelow (stem, kNumStems)) { stems[(size_t) stem] = std::move (p); repaint(); }
    }
    void setStructure (std::vector<StructureRegion> s) { structure = std::move (s); repaint(); }

    // Key-over-time (§6.2 Phase 0): (normalised-position, camelotLabel, colour) segments
    // drawn as a thin strip in the overview so modulations are visible at a glance.
    struct KeyBand { float startNorm = 0.0f, endNorm = 0.0f; juce::String label; juce::Colour colour; };
    void setKeyBands (std::vector<KeyBand> k) { keyBands = std::move (k); repaint(); }
    void setStemMuted (int stem, bool m) { if (juce::isPositiveAndBelow (stem, kNumStems)) { muted[(size_t) stem] = m; repaint(); } }

    void setPlayheadNorm (float p)   { playhead = juce::jlimit (0.0f, 1.0f, p); repaint(); }
    void setTotalSeconds (double s)  { totalSec = s; repaint(); }
    void setBpm (double b)           { bpm = b > 0 ? b : bpm; repaint(); }
    void setZoomBars (int bars)      { zoomBars = juce::jlimit (4, 16, bars); repaint(); }
    void setColourMode (ColourMode m){ colourMode = m; repaint(); }

    int  getZoomBars() const { return zoomBars; }
    ColourMode getColourMode() const { return colourMode; }

    // Local x/width of the four stem columns (VOX/DRM/BAS/OTH), so the host can dock a
    // gain knob directly under each column (§3 spatial alignment). Mirrors paint()'s
    // detail-zone layout.
    std::array<juce::Rectangle<int>, kNumStems> columnRects() const
    {
        auto b = getLocalBounds();
        b.removeFromTop (30);   // top strip (overview + toggle + zoom)
        b.removeFromTop (2);
        auto detail = b.reduced (4, 2);
        detail.removeFromLeft (24);   // bar-number gutter
        const int colGap = 4;
        const int colW = (detail.getWidth() - colGap * (kNumStems - 1)) / kNumStems;
        std::array<juce::Rectangle<int>, kNumStems> out;
        for (int s = 0; s < kNumStems; ++s)
        {
            out[(size_t) s] = detail.removeFromLeft (colW);
            if (s < kNumStems - 1) detail.removeFromLeft (colGap);
        }
        return out;
    }

    // Drag-to-load: keeps a loaded deck droppable even though the region replaced
    // the flat WaveformDisplay drop target.
    std::function<void (const juce::StringArray&)> onFilesDropped;
    std::function<void (float)> onSeekNorm;   // overview click -> jump the playhead (0..1)

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        if (toggleRect.contains (p))
        {
            setColourMode (p.x < toggleRect.getCentreX() ? ColourMode::stems : ColourMode::frequency);
            return;
        }
        if (zoomRect.contains (p))
        {
            const int opts[] = { 4, 8, 16 };
            const int idx = juce::jlimit (0, 2, (p.x - zoomRect.getX()) * 3 / juce::jmax (1, zoomRect.getWidth()));
            setZoomBars (opts[idx]);
            return;
        }
        if (overviewRect.contains (p) && onSeekNorm)
            onSeekNorm (juce::jlimit (0.0f, 1.0f, (float) (p.x - overviewRect.getX()) / (float) overviewRect.getWidth()));
    }
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& f : files)
            if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff")
                || f.endsWithIgnoreCase (".mp3") || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".m4a")
                || f.endsWithIgnoreCase (".ogg"))
                return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override            { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragOver = false; repaint();
        if (onFilesDropped) onFilesDropped (files);
    }
    juce::Rectangle<int> toggleBounds() const { return toggleRect; }
    juce::Rectangle<int> overviewBounds() const { return overviewRect; }

    // Window (normalised 0..1) currently shown in the detail zone.
    void visibleWindow (float& t0, float& t1) const
    {
        const double barSec  = bpm > 0 ? (60.0 / bpm) * 4.0 : 2.0;   // one 4/4 bar
        const double winFrac = totalSec > 0 ? (barSec * zoomBars) / totalSec : 0.1;
        // Asymmetric around the playhead so it maps to the fixed "now" line (kNowFrac
        // of the window is history above the line, the rest is upcoming below).
        t0 = (float) juce::jlimit (0.0, 1.0, (double) playhead - kNowFrac * winFrac);
        t1 = (float) juce::jlimit (0.0, 1.0, (double) playhead + (1.0 - kNowFrac) * winFrac);
        if (t1 <= t0) t1 = juce::jmin (1.0f, t0 + 0.02f);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::bgApp));
        g.fillRoundedRectangle (b.toFloat(), 6.0f);

        // ── Top strip: overview + toggle + zoom ────────────────────────────────
        auto top = b.removeFromTop (30);
        toggleRect = top.removeFromRight (86).reduced (3);
        zoomRect = top.removeFromRight (96).reduced (3);
        overviewRect = top.reduced (4, 3);
        drawOverview (g, overviewRect.toFloat());
        drawToggle (g, toggleRect);
        drawZoom (g, zoomRect);

        // ── Detail: bar gutter + four vertical stem columns ────────────────────
        b.removeFromTop (2);
        auto detail = b.reduced (4, 2);
        auto gutter = detail.removeFromLeft (24);
        drawGutter (g, gutter);

        const int colGap = 4;
        const int colW = (detail.getWidth() - colGap * (kNumStems - 1)) / kNumStems;
        for (int s = 0; s < kNumStems; ++s)
        {
            auto col = detail.removeFromLeft (colW);
            if (s < kNumStems - 1) detail.removeFromLeft (colGap);
            drawStemColumn (g, col, s);
        }

        // "now" line across all columns (fixed vertical position).
        const float nowY = (float) (b.getY() + b.getHeight() * kNowFrac);
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgPrimary).withAlpha (0.9f));
        g.fillRect ((float) (b.getX() + 24), nowY - 0.5f, (float) (b.getWidth() - 24), 1.5f);

        if (dragOver)
        {
            g.setColour (stemdeck::tokens::c (stemdeck::tokens::deckA).withAlpha (0.9f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 2.0f);
        }
    }

private:
    static constexpr float kNowFrac = 0.62f;   // played above, upcoming below

    static float sampleAt (const std::vector<float>& p, float norm)
    {
        if (p.empty()) return 0.0f;
        const int idx = juce::jlimit (0, (int) p.size() - 1, (int) std::round (norm * (float) (p.size() - 1)));
        return p[(size_t) idx];
    }

    void drawOverview (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::bgPanel2));
        g.fillRoundedRectangle (r, 3.0f);

        // Structure regions (shaded + labelled).
        for (const auto& s : structure)
        {
            auto seg = r.withX (r.getX() + s.startNorm * r.getWidth())
                        .withWidth ((s.endNorm - s.startNorm) * r.getWidth());
            g.setColour (s.colour.withAlpha (0.18f));
            g.fillRect (seg);
            g.setColour (s.colour.withAlpha (0.9f));
            g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
            g.drawText (s.label, seg.toNearestInt(), juce::Justification::centred);
        }

        // Whole-track amplitude waveform (mirrored). Frequency colouring needs
        // spectral bins we don't have yet, so freq mode is a neutral amplitude view.
        const float cy = r.getCentreY();
        const int n = juce::jmax (1, (int) r.getWidth());
        juce::Path wave;
        for (int x = 0; x < n; ++x)
        {
            const float norm = (float) x / (float) n;
            const float a = sampleAt (overview, norm) * (r.getHeight() * 0.48f);
            const float px = r.getX() + (float) x;
            wave.addLineSegment ({ px, cy - a, px, cy + a }, 1.0f);
        }
        g.setColour (colourMode == ColourMode::stems ? stemdeck::tokens::c (stemdeck::tokens::deckA).withAlpha (0.65f)
                                                     : stemdeck::tokens::c (stemdeck::tokens::fgSecondary).withAlpha (0.6f));
        g.fillPath (wave);

        // Window box for the detail slice.
        float t0, t1; visibleWindow (t0, t1);
        auto win = r.withX (r.getX() + t0 * r.getWidth()).withWidth (juce::jmax (2.0f, (t1 - t0) * r.getWidth()));
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgPrimary).withAlpha (0.12f));
        g.fillRect (win);
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgPrimary).withAlpha (0.6f));
        g.drawRect (win, 1.0f);

        // Key-over-time strip along the bottom of the overview: one segment per key,
        // labelled at each change so modulations read at a glance (§6.2 Phase 0).
        if (! keyBands.empty())
        {
            auto strip = r.removeFromBottom (9.0f);
            for (const auto& k : keyBands)
            {
                auto seg = strip.withX (strip.getX() + k.startNorm * strip.getWidth())
                                .withWidth (juce::jmax (1.0f, (k.endNorm - k.startNorm) * strip.getWidth()));
                g.setColour (k.colour.withAlpha (0.55f));
                g.fillRect (seg);
                if (seg.getWidth() > 16.0f)
                {
                    g.setColour (stemdeck::tokens::c (stemdeck::tokens::bgApp));
                    g.setFont (juce::FontOptions (7.5f, juce::Font::bold));
                    g.drawText (k.label, seg.toNearestInt(), juce::Justification::centred);
                }
            }
        }

        // Playhead.
        const float px = r.getX() + playhead * r.getWidth();
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::warn2));
        g.fillRect (px - 0.5f, r.getY(), 1.0f, r.getHeight());
    }

    void drawStemColumn (juce::Graphics& g, juce::Rectangle<int> col, int stem)
    {
        const float dim = muted[(size_t) stem] ? 0.4f : 1.0f;
        auto rf = col.toFloat();
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::bgPanel2));
        g.fillRoundedRectangle (rf, 3.0f);

        // Label (always visible, in the stem colour).
        g.setColour (stemColour[(size_t) stem].withMultipliedAlpha (dim));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (stemLabel[(size_t) stem], col.removeFromTop (13), juce::Justification::centred);

        auto body = col.toFloat();
        const auto& peaks = stems[(size_t) stem];
        float t0, t1; visibleWindow (t0, t1);
        const float cx = body.getCentreX();
        const float maxHalf = body.getWidth() * 0.46f;

        // Vertical waveform: time runs top->bottom, playhead maps to the now-line.
        const juce::Colour wc = (colourMode == ColourMode::stems ? stemColour[(size_t) stem]
                                                                 : stemdeck::tokens::c (stemdeck::tokens::fgSecondary))
                                    .withMultipliedAlpha (dim);
        g.setColour (wc);
        const int rows = juce::jmax (1, (int) body.getHeight());
        for (int y = 0; y < rows; ++y)
        {
            const float rowNorm = (float) y / (float) rows;               // 0 top .. 1 bottom
            const float tnorm   = t0 + rowNorm * (t1 - t0);               // track position
            const float a = sampleAt (peaks, tnorm) * maxHalf;
            const float py = body.getY() + (float) y;
            g.fillRect (cx - a, py, a * 2.0f, 1.0f);
        }

        // Beat grid across the column.
        drawGrid (g, body, t0, t1, dim * 0.35f);
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> body, float t0, float t1, float alpha)
    {
        if (bpm <= 0 || totalSec <= 0) return;
        const double beatSec = 60.0 / bpm;
        const double winSec  = (double) (t1 - t0) * totalSec;
        if (winSec <= 0) return;
        const double startSec = (double) t0 * totalSec;
        const double firstBeat = std::ceil (startSec / beatSec) * beatSec;
        for (double t = firstBeat; t < startSec + winSec; t += beatSec)
        {
            const float rn = (float) ((t - startSec) / winSec);
            const float py = body.getY() + rn * body.getHeight();
            const bool bar = std::fmod (std::round (t / beatSec), 4.0) == 0.0;
            g.setColour (stemdeck::tokens::c (stemdeck::tokens::borderSubtle).withAlpha (bar ? alpha * 2.0f : alpha));
            g.fillRect (body.getX(), py, body.getWidth(), bar ? 1.0f : 0.5f);
        }
    }

    void drawGutter (juce::Graphics& g, juce::Rectangle<int> gutter)
    {
        float t0, t1; visibleWindow (t0, t1);
        if (bpm <= 0 || totalSec <= 0) return;
        const double barSec  = (60.0 / bpm) * 4.0;
        const double startSec = (double) t0 * totalSec;
        const double winSec   = (double) (t1 - t0) * totalSec;
        const double firstBar = std::ceil (startSec / barSec) * barSec;
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        for (double t = firstBar; t < startSec + winSec; t += barSec)
        {
            const float rn = (float) ((t - startSec) / winSec);
            const int y = gutter.getY() + 13 + (int) (rn * (gutter.getHeight() - 13));
            const int barNo = (int) std::round (t / barSec) + 1;
            g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgMuted));
            g.drawText (juce::String (barNo), gutter.getX(), y - 6, gutter.getWidth() - 2, 12,
                        juce::Justification::centredRight);
        }
    }

    void drawToggle (juce::Graphics& g, juce::Rectangle<int> r)
    {
        auto rf = r.toFloat();
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::bgControl));
        g.fillRoundedRectangle (rf, 4.0f);
        auto half = r.withWidth (r.getWidth() / 2);
        auto drawHalf = [&] (juce::Rectangle<int> h, const juce::String& t, bool on)
        {
            if (on) { g.setColour (stemdeck::tokens::c (stemdeck::tokens::deckA).withAlpha (0.22f)); g.fillRoundedRectangle (h.toFloat().reduced (1), 3.0f); }
            g.setColour (on ? stemdeck::tokens::c (stemdeck::tokens::deckA) : stemdeck::tokens::c (stemdeck::tokens::fgTertiary));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (t, h, juce::Justification::centred);
        };
        drawHalf (half, "stems", colourMode == ColourMode::stems);
        drawHalf (half.withX (half.getRight()), "freq", colourMode == ColourMode::frequency);
    }

    void drawZoom (juce::Graphics& g, juce::Rectangle<int> r)
    {
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgTertiary));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        const int opts[] = { 4, 8, 16 };
        const int w = r.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto cell = r.withX (r.getX() + i * w).withWidth (w);
            const bool on = zoomBars == opts[i];
            if (on) { g.setColour (stemdeck::tokens::c (stemdeck::tokens::borderSubtle)); g.fillRoundedRectangle (cell.toFloat().reduced (1), 3.0f); }
            g.setColour (on ? stemdeck::tokens::c (stemdeck::tokens::fgPrimary) : stemdeck::tokens::c (stemdeck::tokens::fgMuted));
            g.drawText (juce::String (opts[i]), cell, juce::Justification::centred);
        }
    }

    std::vector<float> overview;
    std::array<std::vector<float>, kNumStems> stems;
    std::array<juce::Colour, kNumStems> stemColour;
    std::array<juce::String, kNumStems> stemLabel;
    std::array<bool, kNumStems> muted { { false, false, false, false } };
    std::vector<StructureRegion> structure;
    std::vector<KeyBand> keyBands;

    float  playhead = 0.0f;
    double totalSec = 0.0, bpm = 124.0;
    int    zoomBars = 8;
    ColourMode colourMode = ColourMode::stems;
    bool   dragOver = false;
    juce::Rectangle<int> toggleRect, overviewRect, zoomRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformRegion)
};

} // namespace stemdeck
