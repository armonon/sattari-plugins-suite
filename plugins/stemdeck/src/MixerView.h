#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
#include "DesignTokens.h"
#include "ChannelTypes.h"

/**
 * Full-session live mixer.
 *
 * The view is deliberately callback-driven: it renders the processor's real
 * channel list without owning DSP state. This keeps the mixer useful for Track
 * Deck, Input, and MIDI Instrument channels while preserving a single mutation
 * path in MainPluginProcessor.
 */
class MixerView final : public juce::Component
{
public:
    int numChannels = 4;
    std::function<void()>             onClose;
    std::function<juce::String(int)>  getName, getSubtitle, getRoute;
    std::function<juce::Colour(int)>  getColour;
    std::function<float(int)>         getGainDb, getPeak;
    std::function<float(int)>         getMasterPeak;
    std::function<void(int,float)>    setGainDb;
    std::function<bool(int)>          isMuted, isSoloed;
    std::function<void(int)>          toggleMute, toggleSolo;

    MixerView()
    {
        setOpaque (true);
        setTitle ("Live Mixer");
        setDescription ("Live channel gain, metering, mute, solo, and routing.");
    }

    static constexpr float minDb = -60.0f, maxDb = 6.0f;

    void paint (juce::Graphics& g) override
    {
        using namespace stemdeck;
        auto bounds = getLocalBounds();
        g.fillAll (tokens::c (tokens::bgApp));

        auto header = bounds.removeFromTop (58);
        g.setColour (tokens::c (tokens::bgPanel));
        g.fillRect (header);
        g.setColour (tokens::c (tokens::deckA));
        g.fillRect (header.removeFromLeft (3));

        auto headerText = header.reduced (18, 8);
        g.setColour (tokens::c (tokens::fgPrimary));
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText ("Live Mixer", headerText.removeFromTop (25),
                    juce::Justification::centredLeft);
        g.setColour (tokens::c (tokens::fgTertiary));
        g.setFont (juce::FontOptions (10.5f));
        g.drawText ("Every active channel · real gain, mute, solo, routing, and meters",
                    headerText, juce::Justification::centredLeft);

        closeRect = header.removeFromRight (40).reduced (11);
        detailRect = header.removeFromRight (96).reduced (2, 11);
        drawPill (g, detailRect, detailed ? "DETAIL ON" : "COMPACT", detailed,
                  tokens::c (tokens::deckA));
        g.setColour (tokens::c (tokens::fgTertiary));
        g.drawLine ((float) closeRect.getX(), (float) closeRect.getY(),
                    (float) closeRect.getRight(), (float) closeRect.getBottom(), 1.7f);
        g.drawLine ((float) closeRect.getX(), (float) closeRect.getBottom(),
                    (float) closeRect.getRight(), (float) closeRect.getY(), 1.7f);

        auto footer = bounds.removeFromBottom (48);
        paintMaster (g, footer.reduced (14, 7));
        auto content = bounds.reduced (12, 10);
        const int n = juce::jlimit (1, (int) faderRects.size(), numChannels);
        const int gap = 7;
        const int stripWidth = juce::jmax (70, (content.getWidth() - gap * (n - 1)) / n);

        for (auto& rect : faderRects) rect = {};
        for (auto& rect : muteRects) rect = {};
        for (auto& rect : soloRects) rect = {};

        for (int i = 0; i < n; ++i)
        {
            auto strip = content.removeFromLeft (
                i == n - 1 ? content.getWidth() : stripWidth);
            if (i != n - 1)
                content.removeFromLeft (gap);
            paintStrip (g, i, strip);
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        const auto point = event.getPosition();
        if (closeRect.contains (point))
        {
            if (onClose) onClose();
            return;
        }
        if (detailRect.contains (point))
        {
            detailed = ! detailed;
            repaint();
            return;
        }
        for (int i = 0; i < numChannels && i < (int) faderRects.size(); ++i)
        {
            if (muteRects[(size_t) i].contains (point))
            {
                if (toggleMute) toggleMute (i);
                repaint();
                return;
            }
            if (soloRects[(size_t) i].contains (point))
            {
                if (toggleSolo) toggleSolo (i);
                repaint();
                return;
            }
            if (faderRects[(size_t) i].contains (point))
            {
                dragChannel = i;
                applyFader (i, point.y);
                return;
            }
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        for (int i = 0; i < numChannels && i < (int) faderRects.size(); ++i)
            if (faderRects[(size_t) i].contains (event.getPosition()))
            {
                if (setGainDb) setGainDb (i, 0.0f);
                repaint();
                return;
            }
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (dragChannel >= 0)
            applyFader (dragChannel, event.getPosition().y);
    }

    void mouseUp (const juce::MouseEvent&) override { dragChannel = -1; }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        for (int i = 0; i < numChannels && i < (int) faderRects.size(); ++i)
            if (faderRects[(size_t) i].contains (event.getPosition()))
            {
                const auto current = getGainDb ? getGainDb (i) : 0.0f;
                if (setGainDb)
                    setGainDb (i, juce::jlimit (minDb, maxDb,
                                               current + wheel.deltaY * 4.0f));
                repaint();
                return;
            }
        Component::mouseWheelMove (event, wheel);
    }

private:
    void paintStrip (juce::Graphics& g, int index, juce::Rectangle<int> strip)
    {
        using namespace stemdeck;
        const auto accent = getColour ? getColour (index)
                                      : tokens::deckAccent (index);
        const bool muted = isMuted && isMuted (index);
        const bool solo = isSoloed && isSoloed (index);
        const float gain = getGainDb ? getGainDb (index) : 0.0f;
        const float peak = juce::jlimit (0.0f, 1.25f,
                                        getPeak ? getPeak (index) : 0.0f);

        g.setColour (tokens::c (tokens::bgPanel));
        g.fillRoundedRectangle (strip.toFloat(), tokens::radiusPanel);
        g.setColour ((solo ? accent : tokens::c (tokens::borderSubtle))
                         .withAlpha (solo ? 0.70f : 1.0f));
        g.drawRoundedRectangle (strip.toFloat().reduced (0.5f),
                                tokens::radiusPanel, 1.0f);
        if (muted)
        {
            g.setColour (tokens::c (tokens::bgApp).withAlpha (0.38f));
            g.fillRoundedRectangle (strip.toFloat(), tokens::radiusPanel);
        }

        auto area = strip.reduced (8, 9);
        auto accentBar = area.removeFromTop (3);
        g.setColour (accent.withAlpha (muted ? 0.35f : 0.95f));
        g.fillRoundedRectangle (accentBar.toFloat(), 1.5f);
        area.removeFromTop (8);

        g.setColour (tokens::c (tokens::fgPrimary));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawFittedText (getName ? getName (index) : "Channel " + juce::String (index + 1),
                          area.removeFromTop (21), juce::Justification::centred, 1);

        auto subtitle = area.removeFromTop (18);
        g.setColour (tokens::c (tokens::fgMuted));
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawFittedText (getSubtitle ? getSubtitle (index) : "CHANNEL",
                          subtitle, juce::Justification::centred, 1);

        if (detailed)
        {
            area.removeFromTop (3);
            auto route = area.removeFromTop (20).reduced (2, 1);
            drawPill (g, route, getRoute ? getRoute (index) : "CENTER",
                      false, accent);
            area.removeFromTop (7);
        }
        else
        {
            area.removeFromTop (10);
        }

        auto controls = area.removeFromBottom (67);
        auto gainReadout = controls.removeFromTop (19);
        g.setColour (gain > 0.0f ? tokens::c (tokens::warn)
                                 : tokens::c (tokens::fgSecondary));
        g.setFont (tokens::numeric (10.5f, true));
        g.drawText (gain <= minDb + 0.1f ? "-∞ dB"
                                         : juce::String (gain, 1) + " dB",
                    gainReadout, juce::Justification::centred);
        controls.removeFromTop (4);
        auto buttons = controls.removeFromTop (29);
        auto mute = buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2);
        auto soloButton = buttons.reduced (2);
        muteRects[(size_t) index] = mute;
        soloRects[(size_t) index] = soloButton;
        drawPill (g, mute, "MUTE", muted, tokens::c (tokens::meterHot));
        drawPill (g, soloButton, "SOLO", solo, accent);

        auto meter = area.removeFromRight (12).reduced (2, 4);
        area.removeFromRight (6);
        g.setColour (tokens::c (tokens::bgWell2));
        g.fillRoundedRectangle (meter.toFloat(), 3.0f);
        auto lit = meter;
        lit.removeFromTop ((int) std::round ((1.0f - juce::jmin (1.0f, peak))
                                             * (float) meter.getHeight()));
        g.setGradientFill (juce::ColourGradient (
            peak > 0.92f ? tokens::c (tokens::meterHot) : accent,
            (float) lit.getCentreX(), (float) lit.getY(),
            tokens::c (tokens::deckA),
            (float) lit.getCentreX(), (float) lit.getBottom(), false));
        g.fillRoundedRectangle (lit.toFloat(), 3.0f);

        auto fader = area.reduced (juce::jmax (0, area.getWidth() / 2 - 18), 5);
        faderRects[(size_t) index] = fader;
        const int centreX = fader.getCentreX();
        g.setColour (tokens::c (tokens::bgControl));
        g.fillRoundedRectangle (
            juce::Rectangle<float> ((float) centreX - 2.0f, (float) fader.getY(),
                                    4.0f, (float) fader.getHeight()), 2.0f);
        const int unityY = dbToY (0.0f, fader);
        g.setColour (tokens::c (tokens::fgFaint));
        g.drawHorizontalLine (unityY, (float) centreX - 11.0f,
                              (float) centreX + 11.0f);
        const int thumbY = dbToY (gain, fader);
        auto thumb = juce::Rectangle<float> ((float) centreX - 16.0f,
                                             (float) thumbY - 6.0f, 32.0f, 12.0f);
        g.setColour (accent.withAlpha (muted ? 0.42f : 0.95f));
        g.fillRoundedRectangle (thumb, tokens::radiusPill);
        g.setColour (tokens::c (tokens::fgPrimary).withAlpha (0.52f));
        g.drawRoundedRectangle (thumb, tokens::radiusPill, 1.0f);
    }

    void paintMaster (juce::Graphics& g, juce::Rectangle<int> area)
    {
        using namespace stemdeck;
        g.setColour (tokens::c (tokens::bgPanel));
        g.fillRoundedRectangle (area.toFloat(), tokens::radiusControl);
        g.setColour (tokens::c (tokens::fgSecondary));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("MASTER", area.removeFromLeft (70),
                    juce::Justification::centred);
        area.removeFromLeft (4);
        for (int channel = 0; channel < 2; ++channel)
        {
            auto meter = area.removeFromTop (area.getHeight() / (2 - channel)).reduced (2, 2);
            g.setColour (tokens::c (tokens::bgWell2));
            g.fillRoundedRectangle (meter.toFloat(), 2.0f);
            const float peak = juce::jlimit (0.0f, 1.0f,
                getMasterPeak ? getMasterPeak (channel) : 0.0f);
            auto fill = meter;
            fill.setWidth ((int) std::round (peak * (float) meter.getWidth()));
            g.setColour (peak > 0.92f ? tokens::c (tokens::meterHot)
                                      : tokens::c (tokens::deckA));
            g.fillRoundedRectangle (fill.toFloat(), 2.0f);
        }
    }

    static void drawPill (juce::Graphics& g, juce::Rectangle<int> rect,
                          const juce::String& text, bool active,
                          juce::Colour accent)
    {
        using namespace stemdeck;
        g.setColour (active ? tokens::activeFill (accent)
                            : tokens::c (tokens::bgControl));
        g.fillRoundedRectangle (rect.toFloat(), tokens::radiusPill);
        g.setColour (active ? tokens::activeBorder (accent)
                            : tokens::c (tokens::borderSubtle));
        g.drawRoundedRectangle (rect.toFloat(), tokens::radiusPill, 1.0f);
        g.setColour (active ? accent : tokens::c (tokens::fgTertiary));
        g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
        g.drawFittedText (text, rect.reduced (3, 0),
                          juce::Justification::centred, 1);
    }

    static int dbToY (float db, juce::Rectangle<int> fader)
    {
        const float proportion = juce::jlimit (
            0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
        return fader.getBottom()
               - (int) std::round (proportion * (float) fader.getHeight());
    }

    static float yToDb (int y, juce::Rectangle<int> fader)
    {
        const float proportion = juce::jlimit (
            0.0f, 1.0f,
            (float) (fader.getBottom() - y)
                / (float) juce::jmax (1, fader.getHeight()));
        return minDb + proportion * (maxDb - minDb);
    }

    void applyFader (int channel, int y)
    {
        if (setGainDb)
            setGainDb (channel, yToDb (y, faderRects[(size_t) channel]));
        repaint();
    }

    bool detailed = true;
    juce::Rectangle<int> closeRect, detailRect;
    std::array<juce::Rectangle<int>, kMaxChannels> faderRects {}, muteRects {}, soloRects {};
    int dragChannel = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};
