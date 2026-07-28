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
        float gain    = 1.0f;    // 0..2 linear
        float level   = 0.0f;    // 0..1 for the meter (input peak / instrument activity)
        bool  monitor = true;    // Input: audible in the mix
        bool  active  = false;   // MidiInstrument: a note is sounding
        int   midiChannel = 0;   // 0 = omni
        bool  clipping = false;
        float lowDb = 0.0f, midDb = 0.0f, highDb = 0.0f;   // -24..+24
        float filter = 0.5f;                               // 0..1, 0.5 = bypass
        bool  muted = false;
        bool  soloed = false;
        int   sourceChannel = -1;                          // Input: device channel, -1 = auto
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
    std::function<void(float)> onGainChanged;   // 0..2 linear
    std::function<void()>      onRemove;
    std::function<void(int,float)> onEqChanged;      // band 0=low 1=mid 2=high, dB
    std::function<void(float)>     onFilterChanged;  // 0..1
    std::function<void()>          onToggleMute;
    std::function<void()>          onToggleSolo;
    std::function<void()>          onCycleSource;    // Input: next device channel / MIDI: next channel

    void setState (State s) { state = std::move (s); repaint(); }
    const State& getState() const noexcept { return state; }

    void paint (juce::Graphics& g) override
    {
        namespace T = stemdeck::tokens;
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
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (state.name, header, juce::Justification::centredLeft, true);

        body.removeFromTop (10);

        // Reserve the control stack from the BOTTOM so knobs always get a usable
        // size; the meter absorbs whatever height is left over. (Sizing the meter
        // first squeezed FILT/GAIN into unusable slivers.)
        const int footerH = 24, muteRowH = 22, filtRowH = 58, eqRowH = 52, gaps = 6 * 4;
        const int reserved = footerH + muteRowH + filtRowH + eqRowH + gaps;
        auto controls = body.removeFromBottom (juce::jmin (reserved, juce::jmax (0, body.getHeight() - 40)));

        auto meterArea = body;
        {
            // Scale gutter on the right so the meter reads as a meter even at rest,
            // matching the master-output panel's language.
            auto scaleCol = meterArea.removeFromRight (juce::jmin (26, meterArea.getWidth() / 3));
            auto m = meterArea.reduced (juce::jmin (18, meterArea.getWidth() / 5), 0);
            g.setColour (raised);
            g.fillRoundedRectangle (m.toFloat(), 5.0f);

            // Segment guides — a meter with structure, not an empty box.
            g.setColour (hair.withAlpha (0.55f));
            for (int seg = 1; seg < 12; ++seg)
            {
                const float y = (float) m.getY() + (float) m.getHeight() * (float) seg / 12.0f;
                g.drawHorizontalLine ((int) y, (float) m.getX() + 2.0f, (float) m.getRight() - 2.0f);
            }

            // dB ticks (0 / -6 / -12 / -18 / -24 over a -30..0 span).
            g.setColour (textSec.withAlpha (0.75f));
            g.setFont (juce::FontOptions (8.0f));
            for (const int tick : { 0, -6, -12, -18, -24 })
            {
                const float t = 1.0f - (float) (-tick) / 30.0f;      // 0 at bottom
                const int y = m.getBottom() - (int) (t * (float) m.getHeight());
                g.drawText (juce::String (tick), scaleCol.getX(), y - 5, scaleCol.getWidth(), 10,
                            juce::Justification::centredLeft);
            }

            const float lvl = juce::jlimit (0.0f, 1.0f, state.level);
            if (lvl > 0.001f)
            {
                auto lit = m.toFloat().withTrimmedTop (m.getHeight() * (1.0f - lvl));
                g.setColour (state.clipping ? T::c (T::meterHot)
                           : lvl > 0.85f    ? T::c (T::warn)
                                            : T::c (T::stemBas));
                g.fillRoundedRectangle (lit, 5.0f);
            }
            g.setColour (hair);
            g.drawRoundedRectangle (m.toFloat().reduced (0.5f), 5.0f, 1.0f);

            if (state.clipping)
            {
                g.setColour (T::c (T::meterHot));
                g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                g.drawText ("CLIP", meterArea.removeFromTop (14), juce::Justification::centred);
            }
        }

        // ── EQ: LOW / MID / HIGH (same -24..+24 dB range as a deck) ──
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
        }

        controls.removeFromTop (6);

        // ── FILT (bipolar: <0.5 low-pass, >0.5 high-pass) + GAIN ──
        {
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
        }

        controls.removeFromTop (6);

        // ── MUTE + source selector ──
        {
            auto row = controls.removeFromTop (juce::jmin (muteRowH, juce::jmax (16, controls.getHeight() / 2)));
            const int cellW = (row.getWidth() - 10) / 3;

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

        controls.removeFromTop (6);

        // ── Footer: monitor toggle (input) or activity lamp (instrument) ──
        auto footer = controls.removeFromTop (juce::jmax (16, juce::jmin (footerH, controls.getHeight())));
        actionBounds = footer;
        g.setColour (raised);
        g.fillRoundedRectangle (footer.toFloat(), 5.0f);

        if (isInput)
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
        const auto p = e.getPosition();
        if (removeBounds.contains (p)) { if (onRemove) onRemove(); return; }
        if (isIdle() && chooseBounds.contains (p) && onCycleSource) { onCycleSource(); return; }
        if (muteBounds.contains (p))   { if (onToggleMute)   onToggleMute();   return; }
        if (soloBounds.contains (p))   { if (onToggleSolo)   onToggleSolo();   return; }
        if (sourceBounds.contains (p)) { if (onCycleSource)  onCycleSource();  return; }
        if (actionBounds.contains (p) && state.type == ChannelType::input && onToggleMonitor)
            onToggleMonitor();
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
                onEqChanged (i, juce::jlimit (-24.0f, 24.0f, base + dy * 0.15f));
                return;
            }

        if (filterBounds.contains (down) && onFilterChanged)
        { onFilterChanged (juce::jlimit (0.0f, 1.0f, state.filter + dy * 0.004f)); return; }

        if (gainBounds.contains (down) && onGainChanged)
        { onGainChanged (juce::jlimit (0.0f, 2.0f, state.gain + dy * 0.01f)); return; }
    }

private:


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
        const int hintW = (int) std::ceil (g.getCurrentFont().getStringWidthFloat (hint));
        g.setFont (juce::FontOptions (T::tSmall));
        const int chipW = (int) std::ceil (g.getCurrentFont().getStringWidthFloat (cta)) + 20;

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

        const float t = juce::jlimit (-1.0f, 1.0f, valueDb / 24.0f);
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
        const float t = juce::jlimit (0.0f, 1.0f, gain * 0.5f);   // 0..2 -> 0..1

        juce::Path track;
        track.addCentredArc (c.x, c.y, rad, rad, 0.0f, start, start + range, true);
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::borderSubtle));
        g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path val;
        val.addCentredArc (c.x, c.y, rad, rad, 0.0f, start, start + range * t, true);
        g.setColour (gain > 1.02f ? accent.interpolatedWith (stemdeck::tokens::c (stemdeck::tokens::warn2), (gain - 1.0f)) : accent);
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
    mutable juce::Rectangle<int> chooseBounds;
    mutable juce::Rectangle<int> removeBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};

} // namespace stemdeck
