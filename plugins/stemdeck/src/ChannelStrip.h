#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// ChannelStrip — the per-type compact strip for NON-TrackDeck channels (spec §6:
// "define a collapsed representation per type").
//
// A track deck's strip is the big four-stem panel. An input or an instrument has
// nothing to say about stems, so drawing them as a deck is misleading. This strip
// gives each type the controls it actually has, inside the SAME frame chrome
// (accent stripe, name, gain, meter) so the app still reads as one instrument:
//
//   Input          : source label, gain, monitor toggle, live level meter + clip
//   MidiInstrument : instrument name, gain, MIDI-channel, activity lamp
//
// Painted, not composed from child components, so N of them cost nothing to lay out.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "ChannelTypes.h"
#include "DesignTokens.h"
#include "GainScale.h"
#include <array>
#include <cmath>

namespace stemdeck
{

class ChannelStrip : public juce::Component
{
public:
    struct State
    {
        ChannelType  type   = ChannelType::input;
        juce::String name;
        juce::Colour accent { 0xff4AD9C4 };
        bool  focused = false;
        float gain    = 1.0f;    // 0..3 linear; unity is the physical midpoint
        float level   = 0.0f;    // 0..1 for the meter (input peak / instrument activity)
        bool  monitor = true;    // Input: audible in the mix
        bool  active  = false;   // MidiInstrument: a note is sounding
        int   midiChannel = 0;   // 0 = omni
        bool  clipping = false;
        float lowDb = 0.0f, midDb = 0.0f, highDb = 0.0f;   // -24..+9.54; 0 = 100%
        float filter = 0.5f;                               // 0..1, 0.5 = bypass
        bool  muted = false;
        bool  soloed = false;
        int   sourceChannel = -1;                          // Input: device channel, -1 = auto
        bool  vocalInput = false;                          // Mic/vocal workflow vs guitar/line rig
        int   editorPage = 0;                              // persisted per source by the host
        // Idle = no instrument chosen / no input device selected. Idle channels
        // recede so they never compete with loaded ones (reference: "IDLE — recedes").
        bool  hasContent = false;
    };

    ChannelStrip() = default;

    // Nothing loaded and nothing arriving. Now only affects the caption and the
    // source CTA — NOT the size. See paint().
    bool isIdle() const noexcept
    { return ! state.hasContent && ! state.active && state.level <= 0.001f; }



    // Hit targets the host wires up.
    std::function<void()>      onToggleMonitor;
    std::function<void()>      onRequestFocus;
    std::function<void(float)> onGainChanged;   // 0..3 linear
    std::function<void()>      onRemove;
    std::function<void(int,float)> onEqChanged;      // band 0=low 1=mid 2=high, dB
    std::function<void(float)>     onFilterChanged;  // 0..1
    std::function<void()>          onToggleMute;
    std::function<void()>          onToggleSolo;
    std::function<void()>          onCycleSource;    // Input: next device channel / MIDI: next channel
    std::function<void(int)>       onSelectEditorPage;
    std::function<void()>          onOpenFx;

    void setState (State s)
    {
        const auto differs = [] (float a, float b, float epsilon)
        {
            return std::abs (a - b) > epsilon;
        };
        const bool changed = s.type != state.type || s.name != state.name
                          || s.accent != state.accent || s.focused != state.focused
                          || differs (s.gain, state.gain, 0.0001f)
                          || differs (s.level, state.level, 0.002f)
                          || s.monitor != state.monitor || s.active != state.active
                          || s.midiChannel != state.midiChannel
                          || s.clipping != state.clipping
                          || differs (s.lowDb, state.lowDb, 0.01f)
                          || differs (s.midDb, state.midDb, 0.01f)
                          || differs (s.highDb, state.highDb, 0.01f)
                          || differs (s.filter, state.filter, 0.001f)
                          || s.muted != state.muted || s.soloed != state.soloed
                          || s.sourceChannel != state.sourceChannel
                          || s.vocalInput != state.vocalInput
                          || s.editorPage != state.editorPage
                          || s.hasContent != state.hasContent;
        if (! changed) return;
        state = std::move (s);
        repaint();
    }
    const State& getState() const noexcept { return state; }

    void paint (juce::Graphics& g) override
    {
        namespace T = stemdeck::tokens;
        gainBounds = actionBounds = filterBounds = muteBounds = soloBounds = sourceBounds = {};
        eqBounds.fill ({});
        editorTabBounds.fill ({});
        chooseBounds = removeBounds = {};

        // The performance surface has one editor and many monitors. Unfocused
        // inputs/instruments retain identity, level, gain and their safety state,
        // but their detailed EQ/routing controls move out of the visual hierarchy
        // until the channel is selected.
        if (! state.focused)
        {
            paintCompact (g, getLocalBounds());
            return;
        }

        auto r = getLocalBounds().toFloat();
        const auto panel   = T::c (T::bgPanel);      // #101014 per tokens (was #131316)
        const auto raised  = T::c (T::bgControl);
        const auto hair    = T::c (T::borderSubtle);
        const auto textPri = T::c (T::fgPrimary);
        const auto textSec = T::c (T::fgTertiary);

        // Channels are ALWAYS drawn at full size, loaded or not. The reference's
        // idle-collapse (a short receded strip) was built and then removed on request:
        // an input you are about to arm should look like a channel, not a placeholder,
        // and a half-height strip made the row read as broken rather than tidy.
        // isIdle() survives only to pick the "no signal"/"waiting" caption below.

        g.setColour (panel);
        g.fillRoundedRectangle (r, T::radiusPanel);
        g.setColour (state.focused ? state.accent.withAlpha (0.9f) : hair);
        g.drawRoundedRectangle (r.reduced (0.5f), T::radiusPanel, state.focused ? 1.6f : 1.0f);

        // Accent stripe — same language as a deck panel.
        g.setColour (state.accent.withAlpha (0.85f));
        g.fillRoundedRectangle (r.removeFromTop (3.0f).reduced (1.0f, 0.0f), 1.5f);

        auto body = getLocalBounds().reduced (10, 8);

        // ── Header: type badge + name ──
        auto header = body.removeFromTop (20);
        const bool isInput = state.type == ChannelType::input;
        const juce::String badge = isInput ? "IN" : "MIDI";
        auto badgeR = header.removeFromLeft (34);
        g.setColour (state.accent.withAlpha (0.18f));
        g.fillRoundedRectangle (badgeR.toFloat(), 4.0f);
        g.setColour (state.accent);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (badge, badgeR, juce::Justification::centred);

        // Remove target: every channel carries its own, so a middle input can be
        // dropped without removing the ones after it.
        auto closeR = header.removeFromRight (16);
        removeBounds = closeR;
        g.setColour (T::c (T::fgMuted));
        g.setFont (juce::FontOptions (T::tControl));
        g.drawText ("x", closeR, juce::Justification::centred);

        header.removeFromLeft (6);
        g.setColour (textPri);
        g.setFont (T::section (12.5f));
        g.drawText (state.name, header, juce::Justification::centredLeft, true);

        body.removeFromTop (8);

        // Every source is a small instrument with its own vocabulary. These tabs
        // deliberately expose only workflows that are wired today; unavailable
        // tuner/amp/plugin promises do not appear as decorative buttons.
        const std::array<juce::String, 4> pageNames = isInput
            ? (state.vocalInput
                ? std::array<juce::String, 4> { "LIVE", "VOICE", "FX", "" }
                : std::array<juce::String, 4> { "INPUT", "TONE", "FX", "" })
            : std::array<juce::String, 4> { "PLAY", "TONE", "MIDI", "FX" };
        const int pageCount = isInput ? 3 : 4;
        const int activePage = juce::jlimit (0, pageCount - 1, state.editorPage);
        auto pageRow = body.removeFromTop (28);
        g.setColour (T::c (T::bgWell2));
        g.fillRoundedRectangle (pageRow.toFloat(), T::radiusControl);
        const int pageWidth = pageRow.getWidth() / pageCount;
        for (int page = 0; page < pageCount; ++page)
        {
            auto cell = pageRow.removeFromLeft (page == pageCount - 1
                                                   ? pageRow.getWidth() : pageWidth);
            editorTabBounds[(size_t) page] = cell;
            if (page == activePage)
            {
                g.setColour (T::c (T::bgControl));
                g.fillRoundedRectangle (cell.reduced (2).toFloat(), T::radiusInner);
            }
            g.setColour (page == activePage ? textPri : textSec);
            g.setFont (T::technical (8.5f, page == activePage));
            g.drawText (pageNames[(size_t) page], cell, juce::Justification::centred);
        }
        body.removeFromTop (8);

        // Reserve the control stack from the BOTTOM so knobs always get a usable
        // size; the meter absorbs whatever height is left over. (Sizing the meter
        // first squeezed FILT/GAIN into unusable slivers.)
        const bool tonePage = activePage == 1;
        const bool fxPage = activePage == pageCount - 1;
        const int footerH = 24, muteRowH = 22, filtRowH = 58, eqRowH = 52;
        // INPUT/PLAY/MIDI are intentionally sparse: they privilege the live meter,
        // gain and routing. TONE earns the EQ/filter rows; FX earns a clear rack
        // entry point. This makes the tabs change workflow instead of merely
        // relabelling an identical control pile.
        const int toneRows = tonePage ? (eqRowH + 6 + filtRowH + 6) : 0;
        const int gainRow = (! tonePage && ! fxPage) ? (filtRowH + 6) : 0;
        const int reserved = footerH + 6 + muteRowH + toneRows + gainRow;
        auto controls = body.removeFromBottom (juce::jmin (reserved, juce::jmax (0, body.getHeight() - 40)));

        auto meterArea = body;
        {
            // Scale gutter on the right so the meter reads as a meter even at rest,
            // matching the master-output panel's language.
            auto scaleCol = meterArea.removeFromRight (juce::jmin (26, meterArea.getWidth() / 3));
            auto m = meterArea.reduced (juce::jmin (18, meterArea.getWidth() / 5), 0);
            g.setColour (T::c (T::bgWell2));
            g.fillRoundedRectangle (m.toFloat(), 3.0f);

            // dB ticks (0 / -6 / -12 / -18 / -24 over a -30..0 span).
            g.setColour (textSec.withAlpha (0.75f));
            g.setFont (T::technical (7.5f));
            for (const int tick : { 0, -6, -12, -18, -24 })
            {
                const float t = 1.0f - (float) (-tick) / 30.0f;      // 0 at bottom
                const int y = m.getBottom() - (int) (t * (float) m.getHeight());
                g.drawText (juce::String (tick), scaleCol.getX(), y - 5, scaleCol.getWidth(), 10,
                            juce::Justification::centredLeft);
            }

            const float lvl = juce::jlimit (0.0f, 1.0f, state.level);
            constexpr int segmentCount = 18;
            constexpr float segmentGap = 1.0f;
            const float segmentHeight = juce::jmax (
                1.0f, ((float) m.getHeight() - segmentGap * (segmentCount - 1))
                          / (float) segmentCount);
            const int litSegments = (int) std::round (lvl * (float) segmentCount);
            for (int segment = 0; segment < segmentCount; ++segment)
            {
                const float fraction = (float) (segment + 1) / (float) segmentCount;
                const auto colour = fraction > 0.90f ? T::c (T::meterHot)
                                  : fraction > 0.72f ? T::c (T::warn)
                                                     : T::c (T::signal);
                const float y = (float) m.getBottom()
                              - (float) (segment + 1) * segmentHeight
                              - (float) segment * segmentGap;
                g.setColour (segment < litSegments ? colour : colour.withAlpha (0.10f));
                g.fillRoundedRectangle ((float) m.getX() + 2.0f, y,
                                        (float) m.getWidth() - 4.0f,
                                        segmentHeight, 0.8f);
            }
            g.setColour (hair);
            g.drawRoundedRectangle (m.toFloat().reduced (0.5f), 5.0f, 1.0f);

            if (state.clipping)
            {
                g.setColour (T::c (T::meterHot));
                g.setFont (T::technical (8.5f, true));
                g.drawText ("CLIP", meterArea.removeFromTop (14), juce::Justification::centred);
            }
        }

        // ── TONE: LOW / MID / HIGH plus filter and gain ──
        if (tonePage)
        {
            auto eqRow = controls.removeFromTop (juce::jmin (eqRowH, controls.getHeight() / 2));
            const char* names[3] = { "LOW", "MID", "HIGH" };
            const float vals[3]  = { state.lowDb, state.midDb, state.highDb };
            const int w = eqRow.getWidth() / 3;
            for (int i = 0; i < 3; ++i)
            {
                auto cell = eqRow.removeFromLeft (i == 2 ? eqRow.getWidth() : w);
                eqBounds[(size_t) i] = cell;
                auto lab = cell.removeFromTop (11);
                g.setColour (textSec);
                g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
                g.drawText (names[i], lab, juce::Justification::centred);
                drawBipolarKnob (g, cell.toFloat().reduced (3.0f), vals[i], state.accent);
            }
            controls.removeFromTop (6);

            auto row = controls.removeFromTop (juce::jmin (filtRowH, controls.getHeight() / 2));
            auto filtCell = row.removeFromLeft (row.getWidth() / 2);
            filterBounds = filtCell;
            auto fl = filtCell.removeFromTop (11);
            g.setColour (textSec);
            g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
            g.drawText ("FILT", fl, juce::Justification::centred);
            drawBipolarKnob (g, filtCell.toFloat().reduced (3.0f), (state.filter - 0.5f) * 48.0f, state.accent);

            gainBounds = row;
            auto gl = row.removeFromTop (11);
            g.setColour (textSec);
            g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
            g.drawText ("GAIN", gl, juce::Justification::centred);
            drawGainArc (g, row.toFloat().reduced (2.0f), state.gain, state.accent);

            controls.removeFromTop (6);
        }

        // INPUT/PLAY/MIDI pages keep the live meter large and give gain a single,
        // unambiguous target. Routing remains in the button row directly below.
        if (! tonePage && ! fxPage)
        {
            auto row = controls.removeFromTop (juce::jmin (filtRowH, controls.getHeight() / 2));
            gainBounds = row;
            auto gl = row.removeFromTop (11);
            g.setColour (textSec);
            g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
            g.drawText ("GAIN", gl, juce::Justification::centred);
            drawGainArc (g, row.toFloat().reduced (2.0f), state.gain, state.accent);
            controls.removeFromTop (6);
        }

        // ── MUTE + source selector ──
        {
            auto row = controls.removeFromTop (juce::jmin (muteRowH, juce::jmax (16, controls.getHeight() / 2)));
            const bool showRouting = ! fxPage;
            const int cellW = (row.getWidth() - (showRouting ? 10 : 5))
                            / (showRouting ? 3 : 2);

            auto muteCell = row.removeFromLeft (cellW);
            muteBounds = muteCell;
            g.setColour (state.muted ? T::c (T::meterHot).withAlpha (0.28f) : raised);
            g.fillRoundedRectangle (muteCell.toFloat(), 4.0f);
            g.setColour (state.muted ? T::c (T::meterHot) : textSec);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText ("MUTE", muteCell, juce::Justification::centred);

            row.removeFromLeft (5);
            auto soloCell = row.removeFromLeft (cellW);
            soloBounds = soloCell;
            g.setColour (state.soloed ? T::c (T::warn).withAlpha (0.30f) : raised);
            g.fillRoundedRectangle (soloCell.toFloat(), 4.0f);
            g.setColour (state.soloed ? T::c (T::warn) : textSec);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText ("SOLO", soloCell, juce::Justification::centred);

            if (showRouting)
            {
                row.removeFromLeft (5);
                sourceBounds = row;
                g.setColour (raised);
                g.fillRoundedRectangle (row.toFloat(), 4.0f);
                g.setColour (textSec);
                g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                const juce::String srcText = isInput
                    ? (state.sourceChannel < 0 ? juce::String ("SRC AUTO") : "SRC " + juce::String (state.sourceChannel + 1))
                    : (state.midiChannel == 0 ? juce::String ("CH OMNI") : "CH " + juce::String (state.midiChannel));
                g.drawText (srcText, row, juce::Justification::centred);
            }
        }

        controls.removeFromTop (6);

        // ── Footer: monitor toggle (input) or activity lamp (instrument) ──
        auto footer = controls.removeFromTop (juce::jmax (16, juce::jmin (footerH, controls.getHeight())));
        actionBounds = footer;
        g.setColour (raised);
        g.fillRoundedRectangle (footer.toFloat(), 5.0f);

        if (fxPage)
        {
            g.setColour (state.accent.withAlpha (0.18f));
            g.fillRoundedRectangle (footer.toFloat(), 5.0f);
            g.setColour (state.accent);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("OPEN CHANNEL FX", footer, juce::Justification::centred);
        }
        else if (isInput)
        {
            const bool on = state.monitor;
            if (on)
            {
                g.setColour (state.accent.withAlpha (0.22f));
                g.fillRoundedRectangle (footer.toFloat(), 5.0f);
            }
            g.setColour (on ? state.accent : textSec);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (on ? "MONITOR ON" : "MONITOR OFF", footer, juce::Justification::centred);
        }
        else
        {
            g.setColour (state.active ? T::c (T::stemBas) : textSec.withAlpha (0.5f));
            g.fillEllipse ((float) footer.getX() + 9.0f, (float) footer.getCentreY() - 4.0f, 8.0f, 8.0f);
            g.setColour (textSec);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (state.active ? "PLAYING" : "IDLE", footer.withTrimmedLeft (22), juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onRequestFocus) onRequestFocus();
        const auto p = e.getPosition();
        for (int page = 0; page < (int) editorTabBounds.size(); ++page)
            if (editorTabBounds[(size_t) page].contains (p))
            {
                state.editorPage = page;
                if (onSelectEditorPage) onSelectEditorPage (page);
                repaint();
                return;
            }
        if (removeBounds.contains (p)) { if (onRemove) onRemove(); return; }
        if (isIdle() && chooseBounds.contains (p) && onCycleSource) { onCycleSource(); return; }
        if (muteBounds.contains (p))   { if (onToggleMute)   onToggleMute();   return; }
        if (soloBounds.contains (p))   { if (onToggleSolo)   onToggleSolo();   return; }
        if (sourceBounds.contains (p)) { if (onCycleSource)  onCycleSource();  return; }
        if (actionBounds.contains (p))
        {
            const int pageCount = state.type == ChannelType::input ? 3 : 4;
            if (state.editorPage == pageCount - 1) { if (onOpenFx) onOpenFx(); }
            else if (state.type == ChannelType::input && onToggleMonitor) onToggleMonitor();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // Vertical drag, same gesture as the deck knobs.
        const auto down = e.getMouseDownPosition();
        const float dy = (float) -e.getDistanceFromDragStartY();

        for (int i = 0; i < 3; ++i)
            if (eqBounds[(size_t) i].contains (down) && onEqChanged)
            {
                const float base = i == 0 ? state.lowDb : i == 1 ? state.midDb : state.highDb;
                const float baseNorm = stemdeck::gainscale::bipolarDbToNorm (
                    base, -24.0f, stemdeck::gainscale::kMaxDb);
                onEqChanged (i, stemdeck::gainscale::normToBipolarDb (
                    baseNorm + dy * 0.005f, -24.0f, stemdeck::gainscale::kMaxDb));
                return;
            }

        if (filterBounds.contains (down) && onFilterChanged)
        { onFilterChanged (juce::jlimit (0.0f, 1.0f, state.filter + dy * 0.004f)); return; }

        if (gainBounds.contains (down) && onGainChanged)
        {
            const float baseNorm = stemdeck::gainscale::linearToControlNorm (state.gain);
            onGainChanged (stemdeck::gainscale::controlNormToLinear (
                baseNorm + dy * 0.005f));
            return;
        }
    }

private:

    void paintCompact (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        namespace T = stemdeck::tokens;
        const bool isInput = state.type == ChannelType::input;
        const auto r = bounds.toFloat();
        g.setColour (T::c (T::bgPanel));
        g.fillRoundedRectangle (r, T::radiusPanel);
        g.setColour (T::c (T::borderSubtle));
        g.drawRoundedRectangle (r.reduced (0.5f), T::radiusPanel, 1.0f);
        g.setColour (state.accent.withAlpha (0.82f));
        g.fillRoundedRectangle (r.getX(), r.getY() + 12.0f, 3.0f,
                                r.getHeight() - 24.0f, 1.5f);

        auto body = bounds.reduced (12, 12);
        auto header = body.removeFromTop (46);
        auto badge = header.removeFromTop (22).removeFromLeft (isInput ? 30 : 40);
        g.setColour (state.accent.withAlpha (0.16f));
        g.fillRoundedRectangle (badge.toFloat(), T::radiusInner);
        g.setColour (state.accent);
        g.setFont (T::technical (9.0f, true));
        g.drawText (isInput ? "IN" : "MIDI", badge, juce::Justification::centred);

        auto close = juce::Rectangle<int> (body.getRight() - 18, body.getY(), 18, 22);
        removeBounds = close;
        g.setColour (T::c (T::fgMuted));
        g.setFont (T::ui (12.0f, true));
        g.drawText (juce::String::fromUTF8 ("\xc3\x97"), close, juce::Justification::centred);

        auto name = body.removeFromTop (30);
        g.setColour (T::c (T::fgPrimary));
        g.setFont (T::section (13.0f));
        g.drawFittedText (state.name, name, juce::Justification::centredLeft, 1);
        body.removeFromTop (8);

        // A tall segmented meter makes the compact object useful at a glance.
        auto meter = body.removeFromTop (juce::jmax (80, body.getHeight() - 132));
        meter = meter.reduced (juce::jmax (8, meter.getWidth() / 5), 0);
        g.setColour (T::c (T::bgWell2));
        g.fillRoundedRectangle (meter.toFloat(), T::radiusInner);
        constexpr int segments = 24;
        const int lit = juce::roundToInt (juce::jlimit (0.0f, 1.0f, state.level) * segments);
        const float gap = 2.0f;
        const float h = ((float) meter.getHeight() - gap * (segments - 1)) / segments;
        for (int i = 0; i < segments; ++i)
        {
            const float fraction = (float) (i + 1) / (float) segments;
            const auto colour = fraction > 0.90f ? T::c (T::meterHot)
                              : fraction > 0.74f ? T::c (T::warn)
                                                 : T::c (T::signal);
            const float y = (float) meter.getBottom() - (float) (i + 1) * h - (float) i * gap;
            g.setColour (i < lit ? colour : colour.withAlpha (0.09f));
            g.fillRoundedRectangle ((float) meter.getX() + 4.0f, y,
                                    (float) meter.getWidth() - 8.0f, h, 1.0f);
        }

        body.removeFromTop (10);
        auto gain = body.removeFromTop (70);
        gainBounds = gain;
        g.setColour (T::c (T::fgTertiary));
        g.setFont (T::technical (8.5f, true));
        g.drawText ("LEVEL", gain.removeFromTop (12), juce::Justification::centred);
        drawGainArc (g, gain.toFloat().reduced (3.0f), state.gain, state.accent);

        body.removeFromTop (6);
        auto status = body.removeFromTop (24);
        actionBounds = status;
        g.setColour ((isInput && state.monitor ? state.accent.withAlpha (0.16f)
                                               : T::c (T::bgControl)));
        g.fillRoundedRectangle (status.toFloat(), T::radiusInner);
        g.setColour (isInput && state.monitor ? state.accent : T::c (T::fgTertiary));
        g.setFont (T::technical (8.5f, true));
        g.drawFittedText (isInput ? (state.monitor ? "MONITORING" : "MONITOR OFF")
                                  : (state.active ? "PLAYING" : "READY"),
                          status.reduced (5, 0), juce::Justification::centred, 1);
    }


    // The receded state: identity + one call to action, at low contrast. Deliberately
    // sparse — the reference's point is that an empty channel must be QUIETER than a
    // loaded one, so this draws no meter, no knobs, no transport.
    // Reference §4 "Channel states — active vs idle". Every value here is taken from
    // stemdeck-components.html rather than chosen: the idle panel is #0D0D10 with a
    // 3px left accent rail at 0.4 alpha (NOT a full border), radius 9, padding 11, the
    // whole thing at 0.85 opacity. Header is one horizontal row — badge, name, and a
    // status word pushed right. Hint and CTA sit side by side on a second row, the CTA
    // a small chip (9px text, 4x10 padding, radius 4), not a full-width button.
    //
    // The collapse is in HEIGHT, not width: the reference uses align-self:flex-start so
    // an idle panel hugs ~84px of content and the row's empty space sits below it. I
    // first built this as a full-height narrow column, which is a different thing.
    void paintIdle (juce::Graphics& g, juce::Rectangle<int> bounds) const
    {
        namespace T = stemdeck::tokens;
        const bool isInput = state.type == ChannelType::input;

        g.setColour (T::c (T::bgIdle));
        g.fillRoundedRectangle (bounds.toFloat(), T::radiusPanel);

        // Left accent rail, clipped to the panel's rounded corners.
        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path rounded;
            rounded.addRoundedRectangle (bounds.toFloat(), T::radiusPanel);
            g.reduceClipRegion (rounded);
            g.setColour (state.accent.withAlpha (0.40f));
            g.fillRect (bounds.removeFromLeft (3));
        }

        auto body = bounds.reduced (T::gap8);

        // ── Header: badge · name · status(right) ──
        auto header = body.removeFromTop (16);
        auto badgeR = header.removeFromLeft (isInput ? 24 : 32);
        g.setColour (state.accent.withAlpha (0.15f));
        g.fillRoundedRectangle (badgeR.toFloat(), T::radiusInner - 1.0f);
        g.setColour (state.accent);
        g.setFont (juce::FontOptions (T::tLabel));
        g.drawText (isInput ? "IN" : "MIDI", badgeR, juce::Justification::centred);

        // Status is pushed to the right edge before the name claims what's left, so a
        // long name truncates instead of colliding with it.
        auto statusR = header.removeFromRight (52);
        g.setColour (T::c (T::fgMuted));
        g.setFont (juce::FontOptions (T::tLabel));
        g.drawText (isInput ? "no signal" : "idle", statusR, juce::Justification::centredRight);

        header.removeFromLeft (T::gap6);
        g.setColour (T::c (T::fgTertiary));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (state.name, header, juce::Justification::centredLeft, true);

        // ── Hint + CTA chip, centred as one group on the second row ──
        body.removeFromTop (T::gap3);
        auto row = body.removeFromTop (30);

        // MIDI copy stays honest: this build's instrument channel is a fixed built-in
        // synth with nothing to pick, so it says what is true rather than the
        // reference's "no instrument loaded" (which presumes a chooser we haven't built).
        const juce::String hint = isInput ? "select source" : "waiting for MIDI";
        const juce::String cta  = isInput ? juce::String (juce::CharPointer_UTF8 ("\xef\xbc\x8b device"))
                                          : (state.midiChannel == 0 ? "omni"
                                                                    : "ch " + juce::String (state.midiChannel));

        g.setFont (juce::FontOptions (T::tBody));
        const int hintW = juce::GlyphArrangement::getStringWidthInt (
            g.getCurrentFont(), hint);
        g.setFont (juce::FontOptions (T::tSmall));
        const int chipW = juce::GlyphArrangement::getStringWidthInt (
            g.getCurrentFont(), cta) + 20;

        const int groupW = hintW + T::gap6 + chipW;
        auto group = row.withSizeKeepingCentre (juce::jmin (groupW, row.getWidth()), 22);

        auto hintR = group.removeFromLeft (juce::jmax (0, group.getWidth() - chipW - T::gap6));
        g.setColour (T::c (T::fgMuted));
        g.setFont (juce::FontOptions (T::tBody));
        g.drawText (hint, hintR, juce::Justification::centredLeft, true);

        group.removeFromLeft (T::gap6);
        chooseBounds = group;
        g.setColour (state.accent.withAlpha (0.12f));
        g.fillRoundedRectangle (group.toFloat(), T::radiusInner - 1.0f);
        g.setColour (state.accent);
        g.setFont (juce::FontOptions (T::tSmall));
        g.drawText (cta, group, juce::Justification::centred);
    }

    // Centre-detented knob for EQ / filter: pointer from 12 o'clock, arc grows out
    // from centre so "flat" is visually obvious.
    static void drawBipolarKnob (juce::Graphics& g, juce::Rectangle<float> r, float valueDb, juce::Colour accent)
    {
        const auto c = r.getCentre();
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.44f;
        if (rad < 4.0f) return;

        const float t = stemdeck::gainscale::bipolarDbToNorm (
                            valueDb, -24.0f, stemdeck::gainscale::kMaxDb) * 2.0f - 1.0f;
        const float halfSweep = juce::MathConstants<float>::pi * 0.75f;

        juce::Path track;
        track.addCentredArc (c.x, c.y, rad, rad, 0.0f, -halfSweep, halfSweep, true);
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::borderSubtle));
        g.strokePath (track, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (std::abs (t) > 0.01f)
        {
            juce::Path val;
            val.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.0f, t * halfSweep, true);
            g.setColour (accent);
            g.strokePath (val, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Pointer
        const float a = t * halfSweep;
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgPrimary).withAlpha (0.9f));
        g.drawLine (c.x + std::sin (a) * rad * 0.35f, c.y - std::cos (a) * rad * 0.35f,
                    c.x + std::sin (a) * rad * 0.92f, c.y - std::cos (a) * rad * 0.92f, 1.6f);
    }

    static void drawGainArc (juce::Graphics& g, juce::Rectangle<float> r, float gain, juce::Colour accent)
    {
        const auto c = r.getCentre();
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.42f;
        if (rad < 4.0f) return;

        const float start = juce::MathConstants<float>::pi * 1.25f;
        const float range = juce::MathConstants<float>::pi * 1.5f;
        const float t = stemdeck::gainscale::linearToControlNorm (gain);

        juce::Path track;
        track.addCentredArc (c.x, c.y, rad, rad, 0.0f, start, start + range, true);
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::borderSubtle));
        g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path val;
        val.addCentredArc (c.x, c.y, rad, rad, 0.0f, start, start + range * t, true);
        const float boost = juce::jlimit (0.0f, 1.0f, (gain - 1.0f)
                                                       / (stemdeck::gainscale::kMaxLinear - 1.0f));
        g.setColour (gain > 1.02f ? accent.interpolatedWith (
                                           stemdeck::tokens::c (stemdeck::tokens::warn2), boost)
                                  : accent);
        g.strokePath (val, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Readout under the arc's open bottom, not across the arc itself.
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgPrimary));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (juce::roundToInt (gain * 100.0f)) + "%",
                    r.toNearestInt().withTop ((int) (c.y + rad * 0.30f)),
                    juce::Justification::centredTop);
    }

    State state;
    juce::Rectangle<int> gainBounds, actionBounds, filterBounds, muteBounds, soloBounds, sourceBounds;
    std::array<juce::Rectangle<int>, 3> eqBounds {};
    std::array<juce::Rectangle<int>, 4> editorTabBounds {};
    mutable juce::Rectangle<int> chooseBounds;
    mutable juce::Rectangle<int> removeBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};

} // namespace stemdeck
