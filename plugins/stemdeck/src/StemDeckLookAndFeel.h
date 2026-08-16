#pragma once

// =============================================================================
//  StemDeck — professional DAW console LookAndFeel
// -----------------------------------------------------------------------------
//  Deep recessed work areas, raised control faces, technical typography and
//  restrained hardware depth. Glow is reserved for signal/state; texture and
//  bevels establish hierarchy without turning the interface into a skin.
// =============================================================================

#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariBranding.h>

// StemDeck-local palette. Referenced by the editor's drawPanel() and the paint
// overrides below. A cool-neutral near-black ground with a cyan accent.
// A single systematic neutral ramp (Tailwind-zinc style) shared by the LookAndFeel,
// the editor chrome, and every component. One scale, applied everywhere, is what
// makes the surface read as cohesive + modern rather than a patchwork of greys.
#include "DesignTokens.h"

// ─────────────────────────────────────────────────────────────────────────────
// DEPRECATED — do not use in new code. Reach for stemdeck::tokens directly.
//
// This namespace is a set of pure ALIASES onto DesignTokens; every value here is
// identical to its token, so there is no colour drift left to fix. What remained
// was a naming problem: two names for one system meant a reader could not tell
// which was authoritative, and the editor mixed both (28 palette references
// against 12 token ones in MainPluginEditor.cpp alone).
//
// Every call site outside this file now names a token. What is left below exists
// only for the `using namespace stemdeck::palette` blocks in the LookAndFeel
// overrides further down, which reference these as bare identifiers. When those
// are converted, delete this namespace entirely.
//
//   kBackground -> bgApp        kHairline      -> borderSubtle
//   kPanel      -> bgPanel      kGlassEdge     -> fgFaint
//   kPanelRaised-> bgControl    kTextPrimary   -> fgPrimary
//   kPanelDeep  -> bgWell       kTextSecondary -> fgSecondary
//   kControlHover->bgControlHover  kTextMuted  -> fgTertiary
//   kWarn       -> warn2        kTextFaint     -> fgMuted
// ─────────────────────────────────────────────────────────────────────────────
namespace stemdeck::palette
{
    namespace T = stemdeck::tokens;
    inline constexpr juce::uint32 kBackground    = T::bgApp;
    inline constexpr juce::uint32 kPanel         = T::bgPanel;        // was #131316
    inline constexpr juce::uint32 kPanelRaised   = T::bgControl;
    inline constexpr juce::uint32 kControlHover  = T::bgControlHover;
    inline constexpr juce::uint32 kPanelDeep     = T::bgWell;         // was #0B0B0D (= ground)
    inline constexpr juce::uint32 kHairline      = T::borderSubtle;
    inline constexpr juce::uint32 kGlassEdge     = T::fgFaint;        // brighter border (kept name)
    inline constexpr juce::uint32 kTextPrimary   = T::fgPrimary;
    inline constexpr juce::uint32 kTextSecondary = T::fgSecondary;
    inline constexpr juce::uint32 kTextMuted     = T::fgTertiary;
    inline constexpr juce::uint32 kTextFaint     = T::fgMuted;
    inline constexpr juce::uint32 kWarn          = T::warn2;          // take marker / hot amber

    // Shared geometry tokens so radii/borders are consistent, not ad-hoc.
    inline constexpr float kRadius   = T::radiusControl;   // default corner
    inline constexpr float kRadiusLg = T::radiusPanel;     // panels (was 8, token is 9)
    inline constexpr float kBorder   = 1.0f;    // hairline stroke
}

class StemDeckLookAndFeel : public sattari::ui::SattariLookAndFeel
{
public:
    explicit StemDeckLookAndFeel (juce::uint32 accentArgb = stemdeck::tokens::selected)
        : sattari::ui::SattariLookAndFeel (accentArgb)
    {
        using namespace stemdeck::palette;
        setColour (juce::Slider::trackColourId, getAccent());
        setColour (juce::Slider::rotarySliderFillColourId, getAccent());
        setColour (juce::Label::textColourId, juce::Colour (kTextPrimary));
        setDefaultSansSerifTypefaceName ("SF Compact");
    }

    // Deep console ground with a very quiet engineering grid. The grid is fixed,
    // cheap to paint, and only visible in unoccupied space.
    void paintModernBackground (juce::Graphics& g, juce::Rectangle<int> bounds) const
    {
        using namespace stemdeck::palette;

        juce::ColourGradient ground (juce::Colour (kBackground).brighter (0.035f),
                                     bounds.getCentreX(), (float) bounds.getY(),
                                     juce::Colour (stemdeck::tokens::bgWell2),
                                     bounds.getCentreX(), (float) bounds.getBottom(), false);
        g.setGradientFill (ground);
        g.fillRect (bounds);

        g.setColour (juce::Colour (stemdeck::tokens::borderFaint).withAlpha (0.18f));
        for (int x = bounds.getX() + 24; x < bounds.getRight(); x += 48)
            g.drawVerticalLine (x, (float) bounds.getY(), (float) bounds.getBottom());
        for (int y = bounds.getY() + 24; y < bounds.getBottom(); y += 48)
            g.drawHorizontalLine (y, (float) bounds.getX(), (float) bounds.getRight());

        // Single crisp hairline frame — structure without chrome.
        const auto frame = bounds.toFloat().reduced (sattari::brand::kPanelInset);
        g.setColour (juce::Colour (kHairline));
        g.drawRoundedRectangle (frame, sattari::brand::kCornerRadius, 1.0f);
    }

    // Hardware-style rotary: recessed track, luminous value arc and a restrained
    // metallic cap. The geometry stays precise enough for small deck controls.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        using namespace stemdeck::palette;
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle  = startAngle + sliderPos * (endAngle - startAngle);
        const auto lineW  = juce::jmax (2.0f, radius * 0.070f);
        const auto arcR   = radius - lineW * 0.5f;
        const auto bodyR  = radius - lineW * 2.4f;                // flat cap inside the ring

        auto fill = slider.findColour (juce::Slider::rotarySliderFillColourId);
        if (fill.isTransparent()) fill = getAccent();

        // Recess and full sweep.
        g.setColour (juce::Colours::black.withAlpha (0.42f));
        g.fillEllipse (juce::Rectangle<float> (bodyR * 2.18f, bodyR * 2.18f)
                           .withCentre (centre).translated (0.0f, 1.5f));
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (kHairline));
        g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Accent value arc.
        if (sliderPos > 0.001f)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
            g.setColour (fill.withAlpha (0.20f));
            g.strokePath (arc, juce::PathStrokeType (lineW + 4.0f,
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
            g.setColour (fill);
            g.strokePath (arc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Brushed-metal cap.
        const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
        juce::ColourGradient metal (juce::Colour (stemdeck::tokens::bgControlHover).brighter (0.12f),
                                    body.getX(), body.getY(),
                                    juce::Colour (stemdeck::tokens::bgTile).darker (0.30f),
                                    body.getRight(), body.getBottom(), false);
        g.setGradientFill (metal);
        g.fillEllipse (body);
        g.setColour (juce::Colour (kHairline));
        g.drawEllipse (body, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.045f));
        g.drawEllipse (body.reduced (2.0f), 1.0f);

        // Single indicator line from cap centre toward the value angle.
        const float ca = std::cos (angle - juce::MathConstants<float>::halfPi);
        const float sa = std::sin (angle - juce::MathConstants<float>::halfPi);
        g.setColour (fill.brighter (0.35f));
        g.drawLine (centre.x + bodyR * 0.30f * ca, centre.y + bodyR * 0.30f * sa,
                    centre.x + bodyR * 0.82f * ca, centre.y + bodyR * 0.82f * sa,
                    juce::jmax (1.5f, lineW * 0.9f));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        using namespace stemdeck::palette;
        auto col = slider.findColour (juce::Slider::trackColourId);
        if (col.isTransparent()) col = getAccent();
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();

        const bool vertical = style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical;
        if (vertical)
        {
            const float cx = bounds.getCentreX();
            const float trackW = 4.0f;
            auto track = juce::Rectangle<float> (cx - trackW * 0.5f, bounds.getY() + 4.0f, trackW, bounds.getHeight() - 8.0f);
            g.setColour (juce::Colours::black.withAlpha (0.72f));
            g.fillRoundedRectangle (track.expanded (1.0f, 0.0f), trackW * 0.5f + 1.0f);
            g.setColour (juce::Colour (kPanelDeep));
            g.fillRoundedRectangle (track, trackW * 0.5f);

            const float top = juce::jlimit (track.getY(), track.getBottom(), sliderPos);
            g.setColour (col.withAlpha (0.55f));   // dimmer level bar; the handle carries the accent
            g.fillRoundedRectangle (juce::Rectangle<float> (track.getX(), top, trackW, track.getBottom() - top), trackW * 0.5f);

            drawFaderCap (g, juce::Rectangle<float> (13.0f, 6.0f).withCentre ({ cx, top }), col);
        }
        else
        {
            const float cy = bounds.getCentreY();
            const float trackH = 4.0f;
            auto track = juce::Rectangle<float> (bounds.getX() + 4.0f, cy - trackH * 0.5f, bounds.getWidth() - 8.0f, trackH);
            g.setColour (juce::Colours::black.withAlpha (0.72f));
            g.fillRoundedRectangle (track.expanded (0.0f, 1.0f), trackH * 0.5f + 1.0f);
            g.setColour (juce::Colour (kPanelDeep));
            g.fillRoundedRectangle (track, trackH * 0.5f);

            const float right = juce::jlimit (track.getX(), track.getRight(), sliderPos);
            g.setColour (col);
            g.fillRoundedRectangle (juce::Rectangle<float> (track.getX(), track.getY(), right - track.getX(), trackH), trackH * 0.5f);

            drawFaderCap (g, juce::Rectangle<float> (6.0f, 20.0f).withCentre ({ right, cy }), col);
        }
    }

    // Tactile control face with semantic roles. Buttons still use the compact
    // console geometry, but primary/record/destructive actions no longer look
    // identical to low-priority utilities.
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool shouldDrawHighlighted, bool shouldDrawDown) override
    {
        using namespace stemdeck::palette;
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();

        // Nav-bar cells: text-only hover/active wash. The active underline is
        // painted by the editor as a single animated bar that slides between cells.
        if (b.getProperties().getWithDefault ("navNaked", false))
        {
            if (on || shouldDrawDown || shouldDrawHighlighted)
            {
                g.setColour (getAccent().withAlpha (on ? 0.12f : 0.06f));
                g.fillRoundedRectangle (r.reduced (2.0f), 6.0f);
            }
            return;
        }

        // Segmented-control tab (reference §3): an iOS-style segment inside a
        // bg-well-2 track. The ACTIVE segment is a raised bg-control chip with no
        // border; inactive segments are fully transparent so the track shows through.
        // Drawing a border per segment is what makes a segmented control read as five
        // separate buttons instead of one control.
        if (b.getProperties().getWithDefault ("segTab", false))
        {
            if (on)
            {
                g.setColour (juce::Colour (kPanelRaised));
                g.fillRoundedRectangle (r, T::radiusControl);
            }
            else if (shouldDrawDown || shouldDrawHighlighted)
            {
                g.setColour (juce::Colour (kPanelRaised).withAlpha (shouldDrawDown ? 0.55f : 0.30f));
                g.fillRoundedRectangle (r, T::radiusControl);
            }
            return;
        }

        // Performance pads: flat cell, coloured hairline border, subtle colour wash
        // when active/pressed. Colour identity without the rubber skeuomorphism.
        if (const int pcv = (int) b.getProperties().getWithDefault ("padColour", 0); pcv != 0)
        {
            const auto pc = juce::Colour ((juce::uint32) pcv);
            const float corner = 7.0f;
            const bool pressed = shouldDrawDown;

            g.setColour (juce::Colour (kPanelRaised));
            g.fillRoundedRectangle (r, corner);
            if (pressed || on || shouldDrawHighlighted)
            {
                g.setColour (pc.withAlpha (pressed ? 0.28f : on ? 0.18f : 0.10f));
                g.fillRoundedRectangle (r, corner);
            }
            g.setColour (pc.withAlpha (on || pressed || shouldDrawHighlighted ? 0.95f : 0.45f));
            g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.2f);
            return;
        }

        const auto role = b.getProperties().getWithDefault ("buttonRole", juce::var()).toString();
        const bool isRecord  = role == "record";
        const bool isPrimary = role == "primary";
        const bool isDanger  = role == "danger";
        const bool isPositive = role == "positive";
        const bool isSync    = role == "sync";
        const bool isAuto    = role == "auto";
        const bool isSafe    = role == "safe";

        auto semantic = b.findColour (juce::TextButton::buttonOnColourId);
        if (semantic.isTransparent()) semantic = getAccent();
        semantic = semantic.withAlpha (1.0f);
        if (isRecord || isDanger) semantic = juce::Colour (T::meterHot);
        else if (isPrimary)       semantic = juce::Colour (T::selected);
        else if (isPositive)      semantic = juce::Colour (T::signal);
        else if (isSync)          semantic = juce::Colour (T::deckA);
        else if (isAuto || isSafe) semantic = juce::Colour (T::warn);

        // Pressing moves the face toward its shadow by one physical pixel. This
        // gives immediate feedback without scaling/blurring text or icons.
        const float corner = kRadius;
        if (! shouldDrawDown)
        {
            g.setColour (juce::Colours::black.withAlpha (0.38f));
            g.fillRoundedRectangle (r.translated (0.0f, 2.0f), corner);
        }
        if (shouldDrawDown)
            r = r.translated (0.0f, 1.0f).withTrimmedBottom (1.0f);

        const bool emphasized = isRecord || isPrimary || isPositive || isSync || isAuto || isSafe;
        juce::Colour top = juce::Colour (kPanelRaised);
        juce::Colour bottom = juce::Colour (stemdeck::tokens::bgTile).darker (0.10f);
        juce::Colour border = juce::Colour (kHairline);
        if (on)
        {
            top = semantic.withAlpha (isRecord ? 0.96f : 0.34f);
            bottom = semantic.withAlpha (isRecord ? 0.70f : 0.19f);
            border = semantic.withAlpha (0.95f);
        }
        else if (emphasized)
        {
            const float idleTop = isRecord ? 0.34f : isSync ? 0.27f : 0.23f;
            const float idleBottom = isRecord ? 0.15f : isSync ? 0.12f : 0.10f;
            top = semantic.withAlpha (shouldDrawHighlighted ? idleTop + 0.10f : idleTop);
            bottom = semantic.withAlpha (shouldDrawHighlighted ? idleBottom + 0.06f : idleBottom);
            border = semantic.withAlpha (shouldDrawHighlighted ? 0.92f : isRecord ? 0.82f : 0.66f);
        }
        else if (isDanger)
        {
            top = semantic.withAlpha (shouldDrawHighlighted ? 0.18f : 0.10f);
            bottom = semantic.withAlpha (0.05f);
            border = semantic.withAlpha (shouldDrawHighlighted ? 0.62f : 0.34f);
        }
        else if (shouldDrawDown)
        {
            top = juce::Colour (kControlHover).darker (0.12f);
            bottom = juce::Colour (kPanelRaised).darker (0.18f);
        }
        else if (shouldDrawHighlighted)
        {
            top = juce::Colour (kControlHover).brighter (0.035f);
            bottom = juce::Colour (kPanelRaised);
            border = juce::Colour (kGlassEdge);
        }

        if (! b.isEnabled())
        {
            top = top.withMultipliedAlpha (0.45f);
            bottom = bottom.withMultipliedAlpha (0.45f);
            border = border.withMultipliedAlpha (0.45f);
        }

        g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(),
                                                 bottom, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, corner);
        // One soft highlight creates separation on dark panels without becoming
        // a glossy/skeuomorphic bezel.
        if (! shouldDrawDown)
        {
            g.setColour (juce::Colours::white.withAlpha (
                on || emphasized ? 0.11f : shouldDrawHighlighted ? 0.085f : 0.045f));
            g.drawLine (r.getX() + corner, r.getY() + 0.75f,
                        r.getRight() - corner, r.getY() + 0.75f, 1.0f);
        }
        g.setColour (border);
        g.drawRoundedRectangle (r.reduced (0.5f), corner, kBorder);
    }

    // Clean button text: medium-weight, fg-primary (accent when toggled on), padded.
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down) override
    {
        using namespace stemdeck::palette;
        const bool on = b.getToggleState();

        // Segmented-control tabs carry state in the CHIP, so their text stays neutral
        // (fg-primary active / fg-tertiary inactive, medium weight when active) per
        // the reference. They also set their own text colours, which the general path
        // below ignores in favour of a hardcoded accent.
        if (b.getProperties().getWithDefault ("segTab", false))
        {
            g.setFont (juce::FontOptions (T::tBody, on ? juce::Font::bold : juce::Font::plain));
            g.setColour (b.findColour (on ? juce::TextButton::textColourOnId
                                          : juce::TextButton::textColourOffId)
                          .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
            g.drawFittedText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
            return;
        }

        const auto role = b.getProperties().getWithDefault ("buttonRole", juce::var()).toString();
        const bool isRecord  = role == "record";
        const bool isPrimary = role == "primary";
        const bool isDanger  = role == "danger";
        const bool isPositive = role == "positive";
        const bool isSync    = role == "sync";
        const bool isAuto    = role == "auto";
        const bool isSafe    = role == "safe";
        auto semantic = b.findColour (juce::TextButton::buttonOnColourId);
        if (semantic.isTransparent()) semantic = getAccent();
        semantic = semantic.withAlpha (1.0f);
        if (isRecord || isDanger) semantic = juce::Colour (T::meterHot);
        else if (isPrimary)       semantic = juce::Colour (T::selected);
        else if (isPositive)      semantic = juce::Colour (T::signal);
        else if (isSync)          semantic = juce::Colour (T::deckA);
        else if (isAuto || isSafe) semantic = juce::Colour (T::warn);

        const float fs = juce::jlimit (8.5f, 12.0f, (float) b.getHeight() * 0.43f);
        g.setFont (T::ui (fs, on || isRecord || isPrimary || isSync || isAuto || isSafe));
        auto text = juce::Colour (kTextPrimary);
        if (on && isRecord) text = juce::Colours::white;
        else if (on || isRecord || isPrimary || isDanger || isPositive || isSync || isAuto || isSafe)
            text = isRecord ? juce::Colours::white.withAlpha (0.96f) : semantic.brighter (0.24f);
        text = text.withMultipliedAlpha (b.isEnabled() ? (over || down ? 1.0f : 0.94f) : 0.38f);
        auto textBounds = b.getLocalBounds().reduced (8, 0);
        if (down) textBounds.translate (0, 1);
        g.setColour (text);
        auto symbol = b.getProperties().getWithDefault ("buttonIcon", juce::var()).toString();
        if (symbol.isEmpty() && isRecord) symbol = "record";
        if (symbol.isNotEmpty() && textBounds.getWidth() >= 44)
        {
            auto icon = textBounds.removeFromLeft (14).withSizeKeepingCentre (12, 12);
            drawSemanticIcon (g, icon.toFloat(), symbol, text);
            textBounds.removeFromLeft (4);
            g.drawFittedText (b.getButtonText(), textBounds, juce::Justification::centredLeft, 1);
        }
        else
        {
            g.drawFittedText (b.getButtonText(), textBounds, juce::Justification::centred, 1);
        }
    }

private:
    static void drawSemanticIcon (juce::Graphics& g, juce::Rectangle<float> bounds,
                                  const juce::String& symbol, juce::Colour colour)
    {
        const auto r = bounds.reduced (1.0f);
        g.setColour (colour);
        if (symbol == "record")
        {
            g.fillEllipse (r.reduced (1.0f));
            return;
        }
        if (symbol == "safe")
        {
            juce::Path shield;
            shield.startNewSubPath (r.getCentreX(), r.getY());
            shield.lineTo (r.getRight(), r.getY() + r.getHeight() * 0.22f);
            shield.lineTo (r.getRight() - 1.0f, r.getCentreY() + 1.0f);
            shield.quadraticTo (r.getCentreX(), r.getBottom(), r.getX() + 1.0f, r.getCentreY() + 1.0f);
            shield.lineTo (r.getX(), r.getY() + r.getHeight() * 0.22f);
            shield.closeSubPath();
            g.strokePath (shield, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
            return;
        }
        if (symbol == "magic")
        {
            juce::Path star;
            star.startNewSubPath (r.getCentreX(), r.getY());
            star.lineTo (r.getCentreX() + 1.5f, r.getCentreY() - 1.5f);
            star.lineTo (r.getRight(), r.getCentreY());
            star.lineTo (r.getCentreX() + 1.5f, r.getCentreY() + 1.5f);
            star.lineTo (r.getCentreX(), r.getBottom());
            star.lineTo (r.getCentreX() - 1.5f, r.getCentreY() + 1.5f);
            star.lineTo (r.getX(), r.getCentreY());
            star.lineTo (r.getCentreX() - 1.5f, r.getCentreY() - 1.5f);
            star.closeSubPath();
            g.fillPath (star);
            return;
        }

        // Two clean opposing arrows communicate global sync without relying on a
        // font glyph, so it renders identically on macOS and Windows.
        juce::Path arrows;
        arrows.startNewSubPath (r.getX() + 1.0f, r.getCentreY() - 2.0f);
        arrows.lineTo (r.getRight() - 2.0f, r.getCentreY() - 2.0f);
        arrows.lineTo (r.getRight() - 4.5f, r.getY() + 1.0f);
        arrows.startNewSubPath (r.getRight() - 1.0f, r.getCentreY() + 2.0f);
        arrows.lineTo (r.getX() + 2.0f, r.getCentreY() + 2.0f);
        arrows.lineTo (r.getX() + 4.5f, r.getBottom() - 1.0f);
        g.strokePath (arrows, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }

    // Slim color-coded handle: a flat block in the control's accent with one bright
    // leading edge, so it reads as a precise handle rather than a stray white bar.
    static void drawFaderCap (juce::Graphics& g, juce::Rectangle<float> cap, juce::Colour col)
    {
        g.setColour (juce::Colours::black.withAlpha (0.48f));
        g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), 2.0f);
        juce::ColourGradient face (col.brighter (0.18f), cap.getX(), cap.getY(),
                                   col.darker (0.24f), cap.getRight(), cap.getBottom(), false);
        g.setGradientFill (face);
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (juce::Colour (stemdeck::tokens::fgPrimary).withAlpha (0.92f));
        if (cap.getWidth() >= cap.getHeight())   // horizontal handle → bright top edge
            g.fillRect (cap.getX() + 1.5f, cap.getY() + 1.0f, cap.getWidth() - 3.0f, 1.3f);
        else                                     // vertical handle → bright left edge
            g.fillRect (cap.getX() + 1.0f, cap.getY() + 1.5f, 1.3f, cap.getHeight() - 3.0f);

        // Two grip cuts make the cap read as a physical fader without adding noise.
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        if (cap.getWidth() >= cap.getHeight())
        {
            g.drawVerticalLine ((int) cap.getCentreX() - 2, cap.getY() + 1.0f, cap.getBottom() - 1.0f);
            g.drawVerticalLine ((int) cap.getCentreX() + 2, cap.getY() + 1.0f, cap.getBottom() - 1.0f);
        }
        else
        {
            g.drawHorizontalLine ((int) cap.getCentreY() - 2, cap.getX() + 1.0f, cap.getRight() - 1.0f);
            g.drawHorizontalLine ((int) cap.getCentreY() + 2, cap.getX() + 1.0f, cap.getRight() - 1.0f);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemDeckLookAndFeel)
};
