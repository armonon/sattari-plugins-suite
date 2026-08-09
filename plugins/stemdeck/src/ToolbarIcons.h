#pragma once

// Vector icons + pill styling for the top-toolbar transport buttons (PLAY ALL /
// REC / AUTO), matching the nav bar's drawn-icon look. Header-only so it can be
// PNG-verified standalone and reused by the editor's paint().

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemdeck
{

enum class ToolIcon { play, stop, rec, magic };

inline void drawToolIcon (juce::Graphics& g, ToolIcon ic, juce::Rectangle<float> b, juce::Colour c)
{
    const float s = juce::jmin (b.getWidth(), b.getHeight());
    b = juce::Rectangle<float> (s, s).withCentre (b.getCentre());
    const float x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight();
    g.setColour (c);
    switch (ic)
    {
        case ToolIcon::play:
        {
            juce::Path p; p.addTriangle (x + w * 0.16f, y + h * 0.08f,
                                         x + w * 0.16f, y + h * 0.92f,
                                         x + w * 0.90f, y + h * 0.50f);
            g.fillPath (p);
            break;
        }
        case ToolIcon::stop:
            g.fillRoundedRectangle (b.reduced (w * 0.16f), 2.0f);
            break;
        case ToolIcon::rec:
            g.fillEllipse (b.reduced (w * 0.18f));
            break;
        case ToolIcon::magic:   // AUTO — a four-point sparkle
        {
            juce::Path p;
            const float cx = b.getCentreX(), cy = b.getCentreY(), r1 = w * 0.42f, r2 = w * 0.14f;
            for (int i = 0; i < 8; ++i)
            {
                const float a = juce::MathConstants<float>::pi * i / 4.0f;
                const float r = (i % 2 == 0) ? r1 : r2;
                const float px = cx + std::cos (a) * r, py = cy + std::sin (a) * r;
                if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
            p.closeSubPath(); g.fillPath (p);
            break;
        }
    }
}

// A polished pill button: gradient fill, accent-tinted when active, icon + tracked
// label. `active` brightens it (e.g. recording, or playing); `hover` lifts it on
// mouse-over.
inline void drawToolButton (juce::Graphics& g, juce::Rectangle<float> r, ToolIcon ic,
                            const juce::String& label, juce::Colour accent, bool active,
                            bool hover = false, bool down = false)
{
    const float radius = 8.0f;
    const float lift = down ? -0.06f : (hover ? 0.10f : 0.0f);
    const auto baseTop = juce::Colour (0xff20242e).brighter (juce::jmax (0.0f, lift));
    const auto baseBot = juce::Colour (0xff161a22).brighter (juce::jmax (0.0f, lift));
    const auto top = active ? accent.withAlpha (down ? 0.82f : 0.92f).brighter (hover ? 0.08f : 0.0f) : baseTop;
    const auto bot = active ? accent.withAlpha (0.72f) : baseBot;
    g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bot, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (active ? accent.brighter (0.3f) : accent.withAlpha (hover ? 0.85f : 0.55f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, active || hover ? 1.4f : 1.2f);

    const auto fg = active ? juce::Colours::white : accent.brighter (hover ? 0.3f : 0.15f);
    auto content = r.reduced (9.0f, 0.0f);
    auto iconArea = content.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 12.0f);
    drawToolIcon (g, ic, iconArea, fg);

    content.removeFromLeft (5.0f);
    juce::Font f (juce::FontOptions (11.0f, juce::Font::bold)); f.setExtraKerningFactor (0.04f);
    g.setColour (active ? juce::Colours::white : juce::Colour (0xffe6ebf3).brighter (hover ? 0.15f : 0.0f));
    g.setFont (f);
    g.drawText (label, content.toNearestInt(), juce::Justification::centredLeft);
}

// A self-painting pill button matching the nav-bar look. Set the icon/label/accent
// once; supply `isActive` to drive the brightened state (e.g. recording / playing).
class ToolButton : public juce::Button
{
public:
    ToolButton (ToolIcon icon, juce::String label, juce::Colour accent)
        : juce::Button (label), ic (icon), text (std::move (label)), col (accent) {}

    void setIcon (ToolIcon icon)          { if (ic != icon) { ic = icon; repaint(); } }
    void setLabel (const juce::String& l) { if (text != l) { text = l; repaint(); } }
    void setAccent (juce::Colour c)       { if (col != c) { col = c; repaint(); } }

    std::function<bool()> isActive;   // optional: true => brightened/active pill

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const bool active = isActive && isActive();
        drawToolButton (g, getLocalBounds().toFloat(), ic, text, col, active, over, down);
    }

private:
    ToolIcon ic;
    juce::String text;
    juce::Colour col;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToolButton)
};

// LookAndFeel that renders a play/pause ICON (not a text toggle) for a deck's play
// ToggleButton. Triangle when stopped, two bars when playing; accent pill while on.
class PlayIconLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Colour accent { 0xff4AD9C4 };
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool playing = b.getToggleState();
        g.setColour (playing ? accent.withAlpha (down ? 0.75f : 0.85f)
                             : juce::Colour (0xff1C1C20).brighter (over ? 0.12f : 0.0f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (playing ? accent.brighter (0.3f) : juce::Colour (0xff2A2A30));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.2f);
        const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.46f;
        auto ic = juce::Rectangle<float> (s, s).withCentre (r.getCentre());
        drawToolIcon (g, playing ? ToolIcon::stop : ToolIcon::play, ic,
                      playing ? juce::Colour (0xff0B0B0D) : juce::Colour (0xffF2F2F5));
    }
};

} // namespace stemdeck
