#pragma once

// CamelotWheel — an interactive harmonic-mixing wheel (perform-screen §6).
//
// Two concentric rings of 12 wedges: outer = major keys (Camelot "nB"), inner =
// minor ("nA"), number 1 at 12 o'clock going clockwise. Unlike djay's blind
// stepper, the GEOMETRY carries the meaning:
//   * the current key is highlighted,
//   * harmonically compatible wedges (same key, relative maj/min, +/-1 on the ring)
//     are lit; everything else is dimmed as the clash zone,
//   * other decks are marked with their accent colour so "is C compatible with me"
//     is a glance, not a memory test,
//   * wedges are shaded by transpose COST (semitone distance) so the cheap moves
//     read as cheap.
// Clicking a wedge IS the transpose control (§6.1): the number is a readout, the
// wheel is the input.
//
// Header-only for standalone PNG verification and reuse by the editor overlay.

#include <juce_gui_basics/juce_gui_basics.h>
#include "DesignTokens.h"

namespace stemdeck
{

// ── Camelot math ────────────────────────────────────────────────────────────
// Camelot number (1..12) for a pitch class + minor flag. Mirrors
// AutomixEngine::camelotNumber so the wheel and the auto-mixer agree.
inline int camelotNumberFor (int tonicPc, bool minor) noexcept
{
    const int pc = ((minor ? tonicPc + 3 : tonicPc) % 12 + 12) % 12;
    const int k  = (pc * 7) % 12;
    return ((7 + k) % 12) + 1;
}

// Pitch class of a Camelot slot (number 1..12, isMinor). Inverse of the above.
inline int pitchClassForCamelot (int number, bool minor) noexcept
{
    const int k  = (number - 1 - 7 + 24) % 12;          // steps of a fifth
    const int pc = (k * 7) % 12;                          // *7 is its own inverse mod 12
    return minor ? ((pc - 3) % 12 + 12) % 12 : pc;
}

// Smallest signed semitone step from one pitch class to another (-6..+6).
inline int semitoneDistance (int fromPc, int toPc) noexcept
{
    int d = ((toPc - fromPc) % 12 + 12) % 12;
    if (d > 6) d -= 12;
    return d;
}

struct CamelotSlot { int number = 1; bool minor = true; };   // e.g. {8,true} = 8A

// Wheel distance on the 12-hour ring (0..6).
inline int wheelRingDistance (int a, int b) noexcept
{
    const int d = std::abs (a - b) % 12;
    return juce::jmin (d, 12 - d);
}

// Compatible = same slot, relative maj/min (same number, other ring), or +/-1 on
// the same ring.
inline bool slotsCompatible (CamelotSlot cur, CamelotSlot other) noexcept
{
    if (cur.number == other.number && cur.minor == other.minor) return true;
    if (cur.number == other.number) return true;                     // relative maj/min
    if (cur.minor == other.minor)   return wheelRingDistance (cur.number, other.number) == 1;
    return false;
}

// ── Drawing ─────────────────────────────────────────────────────────────────
struct CamelotDeckMark { CamelotSlot slot; juce::Colour colour; int deckIndex = 0; bool valid = false; };

inline juce::String camelotLabel (CamelotSlot s) { return juce::String (s.number) + (s.minor ? "A" : "B"); }

// Geometry helper: the wedge annulus for one slot, given the wheel bounds.
inline juce::Path camelotWedgePath (juce::Rectangle<float> b, CamelotSlot s)
{
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float outer = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const float mid   = outer * 0.66f;   // ring boundary
    const float inner = outer * 0.34f;
    const float r0 = s.minor ? inner : mid;
    const float r1 = s.minor ? mid   : outer;
    const float a0 = juce::degreesToRadians ((s.number - 1) * 30.0f - 90.0f - 15.0f);
    const float a1 = a0 + juce::degreesToRadians (30.0f);

    juce::Path p;
    p.addPieSegment (cx - r1, cy - r1, r1 * 2, r1 * 2, a0 + juce::MathConstants<float>::halfPi,
                     a1 + juce::MathConstants<float>::halfPi, r0 / r1);
    return p;
}

inline juce::Point<float> camelotSlotCentre (juce::Rectangle<float> b, CamelotSlot s)
{
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float outer = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const float r = s.minor ? outer * 0.50f : outer * 0.83f;
    const float a = juce::degreesToRadians ((s.number - 1) * 30.0f - 90.0f);
    return { cx + std::cos (a) * r, cy + std::sin (a) * r };
}

// Draw the whole wheel. `current` is the focused deck's key (valid==false => no
// track). `others` marks the other decks.
inline void drawCamelotWheel (juce::Graphics& g, juce::Rectangle<float> area,
                              CamelotSlot current, bool haveCurrent,
                              const std::vector<CamelotDeckMark>& others,
                              CamelotSlot hovered, bool haveHover)
{
    const int curPc = pitchClassForCamelot (current.number, current.minor);

    for (int minor = 0; minor <= 1; ++minor)
        for (int n = 1; n <= 12; ++n)
        {
            const CamelotSlot slot { n, minor == 1 };
            auto path = camelotWedgePath (area, slot);

            const bool isCurrent = haveCurrent && slot.number == current.number && slot.minor == current.minor;
            const bool compat    = haveCurrent && slotsCompatible (current, slot);
            const int  slotPc    = pitchClassForCamelot (slot.number, slot.minor);
            const int  cost      = haveCurrent ? std::abs (semitoneDistance (curPc, slotPc)) : 0;

            // Base fill: shade by transpose cost (cheap = brighter). Compat wedges
            // get a teal wash; the clash zone stays dark.
            juce::Colour fill;
            if (isCurrent)      fill = stemdeck::tokens::c (stemdeck::tokens::deckA);
            else if (compat)    fill = stemdeck::tokens::c (stemdeck::tokens::deckA).withAlpha (0.28f);
            else                fill = stemdeck::tokens::c (stemdeck::tokens::bgControl).brighter (juce::jmax (0.0f, 0.18f - cost * 0.03f));
            const bool isHover = haveHover && slot.number == hovered.number && slot.minor == hovered.minor;
            if (isHover && ! isCurrent) fill = fill.brighter (0.25f);

            g.setColour (fill);
            g.fillPath (path);
            g.setColour (stemdeck::tokens::c (stemdeck::tokens::bgApp));
            g.strokePath (path, juce::PathStrokeType (1.5f));

            // Label.
            const auto c = camelotSlotCentre (area, slot);
            g.setColour (isCurrent ? stemdeck::tokens::c (stemdeck::tokens::bgApp)
                                   : (compat ? stemdeck::tokens::c (stemdeck::tokens::fgPrimary) : stemdeck::tokens::c (stemdeck::tokens::fgMuted)));
            g.setFont (juce::FontOptions (minor == 1 ? 10.0f : 11.0f, juce::Font::bold));
            g.drawText (camelotLabel (slot), (int) c.x - 16, (int) c.y - 8, 32, 16, juce::Justification::centred);
        }

    // Other-deck markers: a coloured dot at that deck's slot centre.
    for (const auto& m : others)
    {
        if (! m.valid) continue;
        const auto c = camelotSlotCentre (area, m.slot);
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::bgApp));
        g.fillEllipse (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f);
        g.setColour (m.colour);
        g.fillEllipse (c.x - 4.5f, c.y - 4.5f, 9.0f, 9.0f);
        g.setColour (stemdeck::tokens::c (stemdeck::tokens::bgApp));
        g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + m.deckIndex)),
                    (int) c.x - 6, (int) c.y - 6, 12, 12, juce::Justification::centred);
    }

    // Centre readout: current key.
    const float outer = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgPrimary));
    g.setFont (juce::FontOptions (outer * 0.22f, juce::Font::bold));
    g.drawText (haveCurrent ? camelotLabel (current) : juce::String ("--"),
                area.toNearestInt(), juce::Justification::centred);
}

// Hit-test: which slot (if any) contains a point.
inline bool camelotSlotAt (juce::Rectangle<float> area, juce::Point<float> p, CamelotSlot& out)
{
    for (int minor = 0; minor <= 1; ++minor)
        for (int n = 1; n <= 12; ++n)
        {
            const CamelotSlot slot { n, minor == 1 };
            if (camelotWedgePath (area, slot).contains (p)) { out = slot; return true; }
        }
    return false;
}

// Interactive wheel component. Clicking a wedge is the transpose control (§6.1):
// it invokes onPick with the chosen slot; the host maps that to a KeyTarget. A
// suggestion line (with reasoning) is drawn beneath the wheel.
class CamelotWheelComponent : public juce::Component
{
public:
    CamelotWheelComponent() { setSize (320, 360); }

    void setCurrent (CamelotSlot s, bool have) { current = s; haveCurrent = have; repaint(); }
    void setOthers (std::vector<CamelotDeckMark> o) { others = std::move (o); repaint(); }
    void setSuggestion (juce::String s) { suggestion = std::move (s); repaint(); }
    bool isPerStem() const { return perStem; }
    // Clicking a wedge transposes; `perStem` selects whole-track vs per-stem key lock (§7).
    std::function<void (CamelotSlot, bool perStem)> onPick;

    juce::Rectangle<float> wheelArea() const
    {
        return getLocalBounds().toFloat().withTrimmedBottom (54.0f).reduced (8.0f);
    }
    juce::Rectangle<int> modeRow() const
    {
        return getLocalBounds().withTrimmedBottom (30).removeFromBottom (22).reduced (10, 1);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (stemdeck::tokens::c (stemdeck::tokens::bgPanel2));
        drawCamelotWheel (g, wheelArea(), current, haveCurrent, others, hovered, haveHover);

        // Whole-track / per-stem mode switch (§7): per-stem holds each tonal stem in
        // key and leaves drums unpitched.
        auto mr = modeRow();
        auto half = mr.withWidth (mr.getWidth() / 2);
        auto seg = [&] (juce::Rectangle<int> h, const juce::String& t, bool on)
        {
            if (on) { g.setColour (stemdeck::tokens::c (stemdeck::tokens::deckA).withAlpha (0.22f)); g.fillRoundedRectangle (h.toFloat().reduced (1), 3.0f); }
            g.setColour (on ? stemdeck::tokens::c (stemdeck::tokens::deckA) : stemdeck::tokens::c (stemdeck::tokens::fgTertiary));
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText (t, h, juce::Justification::centred);
        };
        seg (half, "whole track", ! perStem);
        seg (half.withX (half.getRight()), "per-stem", perStem);

        if (suggestion.isNotEmpty())
        {
            g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgSecondary));
            g.setFont (juce::FontOptions (11.0f));
            g.drawFittedText (suggestion, getLocalBounds().removeFromBottom (30).reduced (10, 4),
                              juce::Justification::centred, 2);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        CamelotSlot s;
        const bool hit = camelotSlotAt (wheelArea(), e.position, s);
        if (hit != haveHover || (hit && (s.number != hovered.number || s.minor != hovered.minor)))
        { haveHover = hit; hovered = s; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { haveHover = false; repaint(); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (modeRow().contains (e.getPosition())) { perStem = e.position.x > (float) modeRow().getCentreX(); repaint(); return; }
        CamelotSlot s;
        if (camelotSlotAt (wheelArea(), e.position, s) && onPick) onPick (s, perStem);
    }

private:
    CamelotSlot current { 8, true }, hovered {};
    bool haveCurrent = false, haveHover = false, perStem = false;
    std::vector<CamelotDeckMark> others;
    juce::String suggestion;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CamelotWheelComponent)
};

} // namespace stemdeck
