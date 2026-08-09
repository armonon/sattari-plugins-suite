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

        auto header = bounds.removeFromTop (68);
        g.setColour (tokens::c (tokens::bgPanel));
        g.fillRect (header);
        g.setColour (tokens::c (tokens::deckA));
        g.fillRect (header.removeFromLeft (3));

        closeRect = header.removeFromRight (42).reduced (12, 20);
        detailRect = header.removeFromRight (104).reduced (2, 18);
        header.removeFromRight (12);
        auto headerText = header.reduced (18, 9);
        g.setColour (tokens::c (tokens::fgPrimary));
        g.setFont (tokens::section (19.0f));
        g.drawText ("LIVE MIXER", headerText.removeFromTop (27),
                    juce::Justification::centredLeft);
        g.setColour (tokens::c (tokens::fgTertiary));
        g.setFont (tokens::technical (9.5f));
        g.drawText ("One console for every deck, input and instrument",
                    headerText, juce::Justification::centredLeft);

        drawPill (g, detailRect, detailed ? "DETAIL ON" : "COMPACT", detailed,
                  tokens::c (tokens::deckA));
        g.setColour (tokens::c (tokens::fgTertiary));
        g.drawLine ((float) closeRect.getX(), (float) closeRect.getY(),
                    (float) closeRect.getRight(), (float) closeRect.getBottom(), 1.7f);
        g.drawLine ((float) closeRect.getX(), (float) closeRect.getBottom(),
                    (float) closeRect.getRight(), (float) closeRect.getY(), 1.7f);

        auto content = bounds.reduced (16, 14);
        const int n = juce::jlimit (1, (int) faderRects.size(), numChannels);

        auto context = content.removeFromTop (28);
        g.setColour (tokens::c (tokens::fgMuted));
        g.setFont (tokens::technical (9.0f, true).withExtraKerningFactor (tokens::trackLabel));
        g.drawText ("CHANNELS  " + juce::String (n), context.removeFromLeft (130),
                    juce::Justification::centredLeft);
        g.setColour (tokens::c (tokens::fgFaint));
        g.drawText ("DOUBLE-CLICK A FADER FOR UNITY  |  SCROLL FOR FINE GAIN",
                    context, juce::Justification::centredRight);
        content.removeFromTop (tokens::gap5);

        const int masterWidth = juce::jlimit (188, 232, content.getWidth() / 5);
        auto masterConsole = content.removeFromRight (masterWidth);
        content.removeFromRight (tokens::gap9);

        const int gap = tokens::gap6;
        const int availableWidth = content.getWidth() - gap * (n - 1);
        const int stripWidth = juce::jlimit (78, 190, availableWidth / n);
        const int groupWidth = stripWidth * n + gap * (n - 1);
        const int consoleHeight = juce::jlimit (440, 650, content.getHeight());
        auto channels = content.withSizeKeepingCentre (juce::jmin (content.getWidth(), groupWidth),
                                                       consoleHeight);

        for (auto& rect : faderRects) rect = {};
        for (auto& rect : muteRects) rect = {};
        for (auto& rect : soloRects) rect = {};

        for (int i = 0; i < n; ++i)
        {
            auto strip = channels.removeFromLeft (stripWidth);
            if (i != n - 1)
                channels.removeFromLeft (gap);
            paintStrip (g, i, strip);
        }

        paintMasterConsole (g, masterConsole);
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
        g.setFont (tokens::section (12.5f));
        g.drawFittedText (getName ? getName (index) : "Channel " + juce::String (index + 1),
                          area.removeFromTop (21), juce::Justification::centred, 1);

        auto subtitle = area.removeFromTop (18);
        g.setColour (tokens::c (tokens::fgMuted));
        g.setFont (tokens::technical (8.0f, true));
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
        g.drawText (gain <= minDb + 0.1f ? "-INF dB"
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

        auto meter = area.removeFromRight (15).reduced (2, 4);
        area.removeFromRight (6);
        peakHold[(size_t) index] = juce::jmax (
            juce::jmin (1.0f, peak), peakHold[(size_t) index] - 0.0065f);
        drawSegmentMeter (g, meter, juce::jmin (1.0f, peak),
                          peakHold[(size_t) index], 22);

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

        // Console scale: labels appear when the strip is wide enough, while the
        // physical tick marks remain useful in compact channel banks.
        g.setFont (tokens::technical (7.0f));
        for (const float tickDb : { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
        {
            const int y = dbToY (tickDb, fader);
            g.setColour (tokens::c (tickDb == 0.0f ? tokens::fgTertiary
                                                   : tokens::fgFaint));
            g.drawHorizontalLine (y, (float) centreX - 9.0f,
                                  (float) centreX + 9.0f);
            if (strip.getWidth() >= 118)
                g.drawText (tickDb > 0.0f ? "+6" : juce::String ((int) tickDb),
                            fader.getX() - 26, y - 5, 23, 10,
                            juce::Justification::centredRight);
        }
        const int thumbY = dbToY (gain, fader);
        auto thumb = juce::Rectangle<float> ((float) centreX - 16.0f,
                                             (float) thumbY - 6.0f, 32.0f, 12.0f);
        g.setColour (accent.withAlpha (muted ? 0.42f : 0.95f));
        g.fillRoundedRectangle (thumb, tokens::radiusPill);
        g.setColour (tokens::c (tokens::fgPrimary).withAlpha (0.52f));
        g.drawRoundedRectangle (thumb, tokens::radiusPill, 1.0f);
    }

    void paintMasterConsole (juce::Graphics& g, juce::Rectangle<int> area)
    {
        using namespace stemdeck;
        g.setColour (tokens::c (tokens::bgPanel));
        g.fillRoundedRectangle (area.toFloat(), tokens::radiusPanel);
        g.setColour (tokens::c (tokens::borderSubtle));
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), tokens::radiusPanel, 1.0f);

        auto content = area.reduced (14, 13);
        auto title = content.removeFromTop (54);
        g.setColour (tokens::c (tokens::deckA));
        g.fillRoundedRectangle (title.removeFromLeft (3).reduced (0, 3).toFloat(), 1.5f);
        title.removeFromLeft (10);
        g.setColour (tokens::c (tokens::fgPrimary));
        g.setFont (tokens::section (15.0f));
        g.drawText ("MASTER OUTPUT", title.removeFromTop (25), juce::Justification::centredLeft);
        g.setColour (tokens::c (tokens::fgTertiary));
        g.setFont (tokens::technical (8.5f, true));
        g.drawText ("STEREO BUS", title, juce::Justification::centredLeft);

        content.removeFromTop (tokens::gap8);
        auto meterZone = content.removeFromTop (juce::jmax (180, content.getHeight() * 57 / 100));
        auto meterLabels = meterZone.removeFromTop (20);
        g.setColour (tokens::c (tokens::fgMuted));
        g.setFont (tokens::technical (8.5f, true));
        g.drawText ("L", meterLabels.removeFromLeft (meterLabels.getWidth() / 2), juce::Justification::centred);
        g.drawText ("R", meterLabels, juce::Justification::centred);

        auto meters = meterZone.reduced (juce::jmax (8, meterZone.getWidth() / 5), 0);
        float maxPeak = 0.0f;
        for (int channel = 0; channel < 2; ++channel)
        {
            auto meter = meters.removeFromLeft (meters.getWidth() / (2 - channel)).reduced (7, 0);
            const float peak = juce::jlimit (0.0f, 1.0f,
                getMasterPeak ? getMasterPeak (channel) : 0.0f);
            maxPeak = juce::jmax (maxPeak, peak);
            masterPeakHold[(size_t) channel] = juce::jmax (
                peak, masterPeakHold[(size_t) channel] - 0.0065f);
            drawSegmentMeter (g, meter, peak, masterPeakHold[(size_t) channel], 28);
        }

        content.removeFromTop (tokens::gap9);
        g.setColour (tokens::c (tokens::borderFaint));
        g.fillRect (content.removeFromTop (1));
        content.removeFromTop (tokens::gap9);

        const float peakDb = juce::Decibels::gainToDecibels (juce::jmax (0.00001f, maxPeak));
        auto peakReadout = content.removeFromTop (58);
        g.setColour (maxPeak > 0.92f ? tokens::c (tokens::meterHot)
                                     : tokens::c (tokens::fgPrimary));
        g.setFont (tokens::numeric (23.0f, true));
        g.drawText (juce::String (peakDb, 1), peakReadout.removeFromTop (32),
                    juce::Justification::centred);
        g.setColour (tokens::c (tokens::fgMuted));
        g.setFont (tokens::technical (8.5f, true));
        g.drawText ("PEAK dBFS", peakReadout, juce::Justification::centred);

        content.removeFromTop (tokens::gap5);
        drawPill (g, content.removeFromTop (24), maxPeak > 0.98f ? "CLIPPING" : "OUTPUT HEALTHY",
                  true, maxPeak > 0.98f ? tokens::c (tokens::meterHot)
                                        : tokens::c (tokens::deckA));
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
        g.setFont (tokens::technical (8.0f, true));
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

    static void drawSegmentMeter (juce::Graphics& g, juce::Rectangle<int> area,
                                  float level, float heldPeak, int segmentCount)
    {
        using namespace stemdeck;
        g.setColour (tokens::c (tokens::bgWell2));
        g.fillRoundedRectangle (area.toFloat(), 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 2.0f, 1.0f);

        const float gap = 1.0f;
        const float segmentH = juce::jmax (
            1.0f, ((float) area.getHeight() - gap * (float) (segmentCount - 1))
                      / (float) segmentCount);
        const int litCount = (int) std::round (
            juce::jlimit (0.0f, 1.0f, level) * (float) segmentCount);
        const int held = juce::jlimit (0, segmentCount - 1,
            (int) std::floor (juce::jlimit (0.0f, 1.0f, heldPeak)
                              * (float) segmentCount));

        for (int segment = 0; segment < segmentCount; ++segment)
        {
            const float fraction = (float) (segment + 1) / (float) segmentCount;
            const auto colour = fraction > 0.90f ? tokens::c (tokens::meterHot)
                              : fraction > 0.72f ? tokens::c (tokens::warn)
                                                 : tokens::c (tokens::signal);
            const bool active = segment < litCount;
            const float y = (float) area.getBottom()
                          - (float) (segment + 1) * segmentH
                          - (float) segment * gap;
            const auto segmentRect = juce::Rectangle<float> (
                (float) area.getX() + 2.0f, y,
                (float) area.getWidth() - 4.0f, segmentH);
            if (active && segment >= segmentCount * 3 / 4)
            {
                g.setColour (colour.withAlpha (0.14f));
                g.fillRoundedRectangle (segmentRect.expanded (1.5f, 0.8f), 1.0f);
            }
            g.setColour (active ? colour : colour.withAlpha (0.10f));
            g.fillRoundedRectangle (segmentRect, 0.8f);

            if (segment == held && heldPeak > 0.01f)
            {
                g.setColour (colour.brighter (0.28f));
                g.fillRoundedRectangle (segmentRect, 0.8f);
            }
        }
    }

    bool detailed = true;
    juce::Rectangle<int> closeRect, detailRect;
    std::array<juce::Rectangle<int>, kMaxChannels> faderRects {}, muteRects {}, soloRects {};
    std::array<float, kMaxChannels> peakHold {};
    std::array<float, 2> masterPeakHold {};
    int dragChannel = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};
