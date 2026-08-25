#pragma once

// Shared drawing/formatting helpers for the MainPluginEditor translation units.
// These used to live in an anonymous namespace at the top of MainPluginEditor.cpp;
// when that file was split (ArrangementRecorderView moved to its own TU) they had
// to be shared. They are `inline` in a named namespace rather than an anonymous
// one so there is a single definition and no unused-function warnings per TU.
// Each editor TU does `using namespace sdedit;` so call sites stay unqualified.

#include "MainPluginEditor.h"
#include "DesignTokens.h"

namespace sdedit
{
inline juce::Colour deckAccent (int deckIndex)
{
    // Accents live in DesignTokens (--deck-a..d), which mirrors tokens.css. This used
    // to hold its own copy of the table, and it had drifted: D was #FFB84A (that is
    // --warn-2) where the token is #E8A54A.
    //
    // It also CLAMPED with jlimit(0,3), so every channel past the fourth came out
    // amber — visible the moment channels stopped being exactly four. Wrap instead.
    return stemdeck::tokens::deckAccent (deckIndex < 0 ? 0 : deckIndex % 4);
}

// Per-stem colours (perform-screen §9). Lanes are drums/bass/music/vocals (1..4)
// with the full-mix at 0; the four separated stems map to DRM/BAS/OTH/VOX.
inline juce::Colour stemLaneColour (int lane)
{
    switch (lane)
    {
        case DeckPlayer::drumsStem:  return juce::Colour (0xff4A9EFF);   // DRM blue
        case DeckPlayer::bassStem:   return juce::Colour (0xff4AD9C4);   // BAS teal
        case DeckPlayer::musicStem:  return juce::Colour (0xff888780);   // OTH neutral
        case DeckPlayer::vocalsStem: return juce::Colour (0xffD4537E);   // VOX rose
        default:                     return juce::Colour (0xff8A8A93);   // full mix
    }
}

// This build decodes bare `char*` string literals as Latin-1, so any literal with
// non-ASCII bytes must be wrapped to be interpreted as UTF-8 or it renders as
// mojibake ("—" → "â€"). u8() forces correct UTF-8 decoding.
inline juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

// Draw uppercase micro-labels with even letter-spacing — the single biggest lever
// for a precise, "engineered" type feel. Centres the tracked string in `area`.
inline void drawTracked (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                  float tracking, juce::Justification just = juce::Justification::centred)
{
    const auto& f = g.getCurrentFont();
    float total = -tracking;
    for (int i = 0; i < text.length(); ++i)
        total += juce::GlyphArrangement::getStringWidth (
                     f, text.substring (i, i + 1)) + tracking;

    const auto a = area.toFloat();
    float x = just.testFlags (juce::Justification::left)  ? a.getX()
            : just.testFlags (juce::Justification::right) ? a.getRight() - total
            : a.getCentreX() - total * 0.5f;
    for (int i = 0; i < text.length(); ++i)
    {
        const juce::String ch = text.substring (i, i + 1);
        const float w = juce::GlyphArrangement::getStringWidth (f, ch);
        g.drawText (ch, juce::Rectangle<float> (x, a.getY(), w + 2.0f, a.getHeight()),
                    juce::Justification::centredLeft, false);
        x += w + tracking;
    }
}

// Fade an overlay view in/out (160/130 ms) instead of snapping — used by the
// MIXER and ARRANGER overlays so view changes feel composed, not abrupt.
inline void fadeOverlay (juce::Component& c, bool show)
{
    auto& anim = juce::Desktop::getInstance().getAnimator();
    // A second navigation request can arrive before the previous 130 ms fade has
    // completed. Cancel the old destination first; otherwise its completion can
    // hide or resurrect a surface after the new layout has already taken ownership.
    anim.cancelAnimation (&c, false);
    if (show)
    {
        c.setVisible (true);
        c.setAlpha (0.0f);
        anim.fadeIn (&c, 160);
    }
    else
    {
        // Primary workspaces are mutually exclusive. Keeping the outgoing view
        // alive during a fade exposes it through the incoming toolbar's gaps.
        c.setAlpha (1.0f);
        c.setVisible (false);
    }
}

// Performance-pad spectrum (cyan → green → blue → violet → gold → orange → red → pink).
inline juce::uint32 padSpectrum (int i)
{
    static const std::array<juce::uint32, 8> cols {
        0xff26d9ff, 0xff62f5c8, 0xff4a90e2, 0xff9b7bff,
        0xffffb020, 0xffe0742b, 0xffe03030, 0xffd44fc8 };
    return cols[(size_t) juce::jlimit (0, 7, i % 8)];
}

inline void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour accent,
                float corner = 10.0f, bool active = false)
{
    namespace sdp = stemdeck::tokens;
    const auto r = bounds.toFloat();

    // Flat matte card.
    g.setColour (juce::Colour (sdp::bgPanel));
    g.fillRoundedRectangle (r, corner);

    // Single border: neutral hairline at rest, accent only when active. No sheen.
    if (active)
    {
        g.setColour (accent.withAlpha (0.80f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.2f);
    }
    else
    {
        g.setColour (juce::Colour (sdp::borderSubtle));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    }
}
}
