#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <array>
#include <functional>

/**
 * Channel-strip mixer overlay for the arrangement deck lanes.
 *
 * Decoupled via callbacks: the editor supplies name/colour/gain/mute/solo getters
 * and setters that read & write the arrangement view's per-lane state, so the
 * mixer and the lane headers stay in sync.
 */
class MixerView final : public juce::Component
{
public:
    int numChannels = 4;
    std::function<void()>             onClose;
    std::function<juce::String(int)>  getName;
    std::function<juce::Colour(int)>  getColour;
    std::function<float(int)>         getGainDb;
    std::function<void(int,float)>    setGainDb;
    std::function<bool(int)>          isMuted, isSoloed;
    std::function<void(int)>          toggleMute, toggleSolo;

    MixerView() { setOpaque (true); }

    static constexpr float minDb = -60.0f, maxDb = 6.0f;

    void paint (juce::Graphics& g) override
    {
        using namespace sattari::brand;
        auto b = getLocalBounds();
        g.fillAll (juce::Colour (kPanelDeep).darker (0.2f));

        auto title = b.removeFromTop (titleH);
        g.setColour (juce::Colour (kPanelRaised)); g.fillRect (title);
        g.setColour (juce::Colour (kAccent));      g.fillRect (title.removeFromLeft (3));
        g.setColour (juce::Colour (kTextPrimary)); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("MIXER", title.reduced (10, 0), juce::Justification::centredLeft);
        closeRect = title.removeFromRight (30).reduced (8);
        g.setColour (juce::Colour (kTextMuted));
        g.drawLine ((float) closeRect.getX(), (float) closeRect.getY(), (float) closeRect.getRight(), (float) closeRect.getBottom(), 1.6f);
        g.drawLine ((float) closeRect.getX(), (float) closeRect.getBottom(), (float) closeRect.getRight(), (float) closeRect.getY(), 1.6f);

        b.reduce (8, 8);
        const int n = juce::jlimit (1, (int) faderR.size(), numChannels);
        const int stripW = b.getWidth() / n;
        for (int i = 0; i < n; ++i)
        {
            auto strip = b.removeFromLeft (stripW).reduced (5, 0);

            auto top = strip.removeFromTop (18);
            if (getColour) { g.setColour (getColour (i)); g.fillRect (top.removeFromLeft (3).withTrimmedBottom (2)); }
            g.setColour (juce::Colour (kTextPrimary).withAlpha (0.9f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (getName ? getName (i) : juce::String (i), top.reduced (4, 0), juce::Justification::centredLeft);

            auto msRow = strip.removeFromBottom (18);
            auto mR = msRow.removeFromLeft (msRow.getWidth() / 2).reduced (2, 1);
            auto sR = msRow.reduced (2, 1);
            muteR[(size_t) i] = mR; soloR[(size_t) i] = sR;
            auto tag = [&] (juce::Rectangle<int> r, const char* t, bool on, juce::Colour c)
            {
                g.setColour (on ? c.withAlpha (0.9f) : juce::Colour (kPanelRaised).withAlpha (0.85f));
                g.fillRoundedRectangle (r.toFloat(), 2.5f);
                g.setColour (on ? juce::Colours::black.withAlpha (0.85f) : juce::Colour (kTextMuted));
                g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                g.drawText (t, r, juce::Justification::centred);
            };
            tag (mR, "M", isMuted  && isMuted  (i), juce::Colour (kDanger));
            tag (sR, "S", isSoloed && isSoloed (i), juce::Colour (kAccent));

            auto dbRow = strip.removeFromBottom (16);
            const float db = getGainDb ? getGainDb (i) : 0.0f;
            g.setColour (juce::Colour (kTextMuted)); g.setFont (juce::FontOptions (9.5f));
            g.drawText (juce::String (db, 1) + " dB", dbRow, juce::Justification::centred);

            auto fader = strip.reduced (0, 6);
            faderR[(size_t) i] = fader;
            const int cx = fader.getCentreX();
            g.setColour (juce::Colour (kHairline).withAlpha (0.5f));
            g.fillRect (cx - 1, fader.getY(), 2, fader.getHeight());
            const int uy = dbToY (0.0f, fader);   // unity tick
            g.setColour (juce::Colour (kTextFaint).withAlpha (0.5f));
            g.drawHorizontalLine (uy, (float) (cx - 8), (float) (cx + 8));
            const int ty = dbToY (db, fader);      // thumb
            auto thumb = juce::Rectangle<int> (cx - 14, ty - 5, 28, 10).toFloat();
            g.setColour (getColour ? getColour (i).withAlpha (0.9f) : juce::Colour (kAccent));
            g.fillRoundedRectangle (thumb, 2.5f);
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawRoundedRectangle (thumb, 2.5f, 1.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (closeRect.contains (e.getPosition())) { if (onClose) onClose(); return; }
        for (int i = 0; i < numChannels && i < (int) faderR.size(); ++i)
        {
            if (muteR[(size_t) i].contains (e.getPosition())) { if (toggleMute) toggleMute (i); repaint(); return; }
            if (soloR[(size_t) i].contains (e.getPosition())) { if (toggleSolo) toggleSolo (i); repaint(); return; }
            if (faderR[(size_t) i].contains (e.getPosition())) { dragChan = i; applyFader (i, e.getPosition().y); return; }
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragChan >= 0) applyFader (dragChan, e.getPosition().y); }
    void mouseUp   (const juce::MouseEvent&)      override { dragChan = -1; }

private:
    int dbToY (float db, juce::Rectangle<int> f) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
        return f.getBottom() - (int) std::round (t * (float) f.getHeight());
    }
    float yToDb (int y, juce::Rectangle<int> f) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, (float) (f.getBottom() - y) / (float) juce::jmax (1, f.getHeight()));
        return minDb + t * (maxDb - minDb);
    }
    void applyFader (int i, int y) { if (setGainDb) setGainDb (i, yToDb (y, faderR[(size_t) i])); repaint(); }

    static constexpr int titleH = 26;
    juce::Rectangle<int> closeRect;
    std::array<juce::Rectangle<int>, 8> faderR {}, muteR {}, soloR {};
    int dragChan = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};
