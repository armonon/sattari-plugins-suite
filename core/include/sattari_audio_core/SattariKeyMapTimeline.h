//  Sattari Audio Suite — Key Map timeline.
//
//  Draws a scanned song as a waveform with its detected KEY SECTIONS overlaid:
//  each segment gets its own colour band + label ("G major"), with a live playhead
//  marker. This replaces the text-only key-map readout — the point is that a
//  producer can SEE where the song modulates, then let Auto Pitch follow it.
//
//  Pure view: it owns no audio and does no analysis. The host plugin scans the file
//  (SattariAutoPitchAudioProcessor::startKeyMapScan) and pushes the resulting
//  envelope + segments in via setData(); Use Key Map then drives the live key.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <sattari_audio_core/SattariBranding.h>
#include <functional>
#include <vector>

namespace sattari::ui
{

struct KeyMapSegment
{
    double timeSec   = 0.0;
    int    root      = 0;    // 0..11 (C..B)
    int    scale     = 0;    // 0 major, 1 minor, 2 chromatic
    float  confidence = 0.0f;
};

class KeyMapTimeline : public juce::Component
{
public:
    KeyMapTimeline() { setInterceptsMouseClicks (true, false); }

    void setData (std::vector<float> waveIn, std::vector<KeyMapSegment> segsIn, double durationSecIn)
    {
        wave = std::move (waveIn);
        segs = std::move (segsIn);
        durationSec = durationSecIn > 0.0 ? durationSecIn : 0.0;
        repaint();
    }

    void clear() { wave.clear(); segs.clear(); durationSec = 0.0; playheadSec = 0.0; repaint(); }

    // Cheap: only repaints when the marker actually moves a pixel.
    void setPlayhead (double sec)
    {
        if (durationSec <= 0.0) return;
        const int oldX = xForTime (playheadSec), newX = xForTime (sec);
        playheadSec = sec;
        if (oldX != newX) repaint();
    }

    bool hasData() const noexcept { return ! segs.empty() && durationSec > 0.0; }

    // Click a section -> callback with its start time (host can't be seeked from a
    // plugin, so the owner decides what to do: preview, select, or nothing).
    std::function<void (double)> onSegmentClicked;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! hasData() || onSegmentClicked == nullptr) return;
        const double t = timeForX (e.x);
        for (int i = (int) segs.size() - 1; i >= 0; --i)
            if (t >= segs[(size_t) i].timeSec) { onSegmentClicked (segs[(size_t) i].timeSec); return; }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (sattari::brand::kPanelDeep));
        g.fillRoundedRectangle (r, 6.0f);

        if (! hasData())
        {
            g.setColour (juce::Colour (sattari::brand::kTextMuted));
            g.setFont (juce::Font (sattari::brand::fontOptions (12.0f)));
            g.drawText (juce::String::fromUTF8 ("Scan a track to map its key changes \xe2\x80\x94 then Use Key Map follows them."),
                        getLocalBounds(), juce::Justification::centred);
            return;
        }

        const int w = getWidth(), h = getHeight();
        const int labelH = 15;                       // strip along the top for key names
        const int waveTop = labelH + 2, waveH = juce::jmax (8, h - waveTop - 12);
        const float mid = (float) waveTop + waveH * 0.5f;

        // --- key section bands ------------------------------------------------
        for (size_t i = 0; i < segs.size(); ++i)
        {
            const double t0 = segs[i].timeSec;
            const double t1 = (i + 1 < segs.size()) ? segs[i + 1].timeSec : durationSec;
            const int x0 = xForTime (t0), x1 = xForTime (t1);
            if (x1 <= x0) continue;
            const auto col = colourForKey (segs[i].root, segs[i].scale);
            g.setColour (col.withAlpha (0.16f));
            g.fillRect (x0, waveTop, x1 - x0, waveH);
            if (i > 0)                                // modulation boundary
            {
                g.setColour (col.withAlpha (0.75f));
                g.fillRect (x0, waveTop, 2, waveH);
            }
            // label (only when the band is wide enough to read)
            if (x1 - x0 > 44)
            {
                g.setColour (col);
                g.setFont (juce::Font (sattari::brand::fontOptions (11.0f, juce::Font::bold)));
                g.drawText (keyName (segs[i].root, segs[i].scale),
                            juce::Rectangle<int> (x0 + 4, 0, x1 - x0 - 6, labelH),
                            juce::Justification::centredLeft, true);
            }
        }

        // --- waveform (peak envelope, mirrored) --------------------------------
        g.setColour (juce::Colour (sattari::brand::kTextMuted).withAlpha (0.85f));
        const int n = (int) wave.size();
        if (n > 0)
        {
            for (int x = 0; x < w; ++x)
            {
                const int i0 = (int) ((double) x / juce::jmax (1, w) * n);
                const int i1 = juce::jlimit (i0 + 1, n, (int) ((double) (x + 1) / juce::jmax (1, w) * n));
                float pk = 0.0f;
                for (int i = i0; i < i1 && i < n; ++i) pk = juce::jmax (pk, wave[(size_t) i]);
                const float half = juce::jmax (0.5f, pk * waveH * 0.48f);
                g.fillRect ((float) x, mid - half, 1.0f, half * 2.0f);
            }
        }

        // --- playhead ----------------------------------------------------------
        if (playheadSec > 0.0 && playheadSec <= durationSec)
        {
            const int px = xForTime (playheadSec);
            g.setColour (juce::Colour (sattari::brand::kAccent));
            g.fillRect (px, waveTop, 2, waveH);
        }

        // --- time ruler --------------------------------------------------------
        g.setColour (juce::Colour (sattari::brand::kTextMuted).withAlpha (0.7f));
        g.setFont (juce::Font (sattari::brand::fontOptions (9.0f)));
        const int ticks = juce::jlimit (2, 8, w / 90);
        for (int t = 0; t <= ticks; ++t)
        {
            const double sec = durationSec * (double) t / ticks;
            const int x = xForTime (sec);
            const int mm = (int) (sec / 60.0), ss = (int) sec % 60;
            g.drawText (juce::String (mm) + ":" + juce::String (ss).paddedLeft ('0', 2),
                        juce::Rectangle<int> (juce::jlimit (0, w - 34, x - 16), h - 11, 34, 10),
                        juce::Justification::centred, false);
        }
    }

    // 12 distinct hues by root; minor is darker//desaturated so mode reads at a glance.
    static juce::Colour colourForKey (int root, int scale)
    {
        const float hue = (float) ((root % 12 + 12) % 12) / 12.0f;
        if (scale == 2) return juce::Colour (sattari::brand::kTextMuted);      // chromatic
        return scale == 1 ? juce::Colour::fromHSV (hue, 0.45f, 0.72f, 1.0f)    // minor
                          : juce::Colour::fromHSV (hue, 0.70f, 0.95f, 1.0f);   // major
    }

    static juce::String keyName (int root, int scale)
    {
        static const char* N[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        const juce::String r (N[((root % 12) + 12) % 12]);
        return scale == 2 ? r + " chromatic" : (scale == 1 ? r + " minor" : r + " major");
    }

private:
    int xForTime (double sec) const
    {
        if (durationSec <= 0.0) return 0;
        return juce::jlimit (0, getWidth(), (int) (sec / durationSec * getWidth()));
    }
    double timeForX (int x) const
    {
        if (getWidth() <= 0) return 0.0;
        return juce::jlimit (0.0, durationSec, (double) x / getWidth() * durationSec);
    }

    std::vector<float>        wave;
    std::vector<KeyMapSegment> segs;
    double durationSec = 0.0;
    double playheadSec = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyMapTimeline)
};

} // namespace sattari::ui
