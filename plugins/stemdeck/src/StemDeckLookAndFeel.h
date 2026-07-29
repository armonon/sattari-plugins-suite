#pragma once

// =============================================================================
//  StemDeck — flat, modern-minimal LookAndFeel
// -----------------------------------------------------------------------------
//  Near-black matte surfaces, a single electric-cyan accent, hairline dividers,
//  thin-ring knobs, flat faders and buttons. No skeuomorphism: no film grain,
//  no backdrop photo, no glowing domes, no rubber bevels. Generous negative
//  space and precise geometry do the work. Palette is scoped to
//  stemdeck::palette so no other plugin in the suite is affected.
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
    explicit StemDeckLookAndFeel (juce::uint32 accentArgb = sattari::brand::kAccent)
        : sattari::ui::SattariLookAndFeel (accentArgb)
    {
        using namespace stemdeck::palette;
        setColour (juce::Slider::trackColourId, getAccent());
        setColour (juce::Slider::rotarySliderFillColourId, getAccent());
        setColour (juce::Label::textColourId, juce::Colour (kTextPrimary));
        // Follow the host OS. On macOS this resolves to San Francisco and tracks
        // accessibility/system typography changes instead of pinning Avenir.
        setDefaultSansSerifTypefaceName (juce::Font::getDefaultSansSerifFontName());
    }

    // Flat near-black ground with a single hairline frame. No photo, grain, or glow.
    void paintModernBackground (juce::Graphics& g, juce::Rectangle<int> bounds) const
    {
        using namespace stemdeck::palette;

        // Barely-there vertical lift keeps a pure-black OLED panel from feeling dead,
        // without reading as a gradient. Two steps, a couple of values apart.
        juce::ColourGradient ground (juce::Colour (kBackground).brighter (0.015f),
                                     bounds.getCentreX(), (float) bounds.getY(),
                                     juce::Colour (kBackground),
                                     bounds.getCentreX(), (float) bounds.getBottom() * 0.6f, false);
        g.setGradientFill (ground);
        g.fillRect (bounds);

        // Single crisp hairline frame — structure without chrome.
        const auto frame = bounds.toFloat().reduced (sattari::brand::kPanelInset);
        g.setColour (juce::Colour (kHairline));
        g.drawRoundedRectangle (frame, sattari::brand::kCornerRadius, 1.0f);
    }

    // Thin-ring knob: hairline track arc, accent value arc, flat matte cap with a
    // single indicator line. No dome, glow, shadow, or bitmap.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        using namespace stemdeck::palette;
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle  = startAngle + sliderPos * (endAngle - startAngle);
        const auto lineW  = juce::jmax (2.0f, radius * 0.055f);   // thin ring
        const auto arcR   = radius - lineW * 0.5f;
        const auto bodyR  = radius - lineW * 2.4f;                // flat cap inside the ring

        auto fill = slider.findColour (juce::Slider::rotarySliderFillColourId);
        if (fill.isTransparent()) fill = getAccent();

        // Hairline track arc (full sweep).
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (kHairline));
        g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Accent value arc.
        if (sliderPos > 0.001f)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
            g.setColour (fill);
            g.strokePath (arc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Flat matte cap.
        const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
        g.setColour (juce::Colour (kPanelRaised));
        g.fillEllipse (body);
        g.setColour (juce::Colour (kHairline));
        g.drawEllipse (body, 1.0f);

        // Single indicator line from cap centre toward the value angle.
        const float ca = std::cos (angle - juce::MathConstants<float>::halfPi);
        const float sa = std::sin (angle - juce::MathConstants<float>::halfPi);
        g.setColour (juce::Colour (kTextPrimary));
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
            g.setColour (juce::Colour (kPanelDeep));  g.fillRoundedRectangle (track, trackW * 0.5f);

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
            g.setColour (juce::Colour (kPanelDeep));  g.fillRoundedRectangle (track, trackH * 0.5f);

            const float right = juce::jlimit (track.getX(), track.getRight(), sliderPos);
            g.setColour (col);
            g.fillRoundedRectangle (juce::Rectangle<float> (track.getX(), track.getY(), right - track.getX(), trackH), trackH * 0.5f);

            drawFaderCap (g, juce::Rectangle<float> (6.0f, 20.0f).withCentre ({ right, cy }), col);
        }
    }

    // Flat button: matte fill, hairline border, accent fill + border when active.
    // No pills, no frosted sheen, no bevels.
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

        // Clean flat button (Tailwind-style): neutral fill, subtle hover lift, an
        // accent-tinted fill + accent border only when toggled on. Consistent radius.
        const float corner = kRadius;
        juce::Colour fill = juce::Colour (kPanelRaised);
        juce::Colour border = juce::Colour (kHairline);
        if (on)
        {
            fill   = getAccent().withAlpha (shouldDrawDown ? 0.30f : 0.18f);
            border = getAccent().withAlpha (0.55f);
        }
        else if (shouldDrawDown)        { fill = juce::Colour (kControlHover).darker (0.12f); }
        else if (shouldDrawHighlighted) { fill = juce::Colour (kControlHover); border = juce::Colour (kGlassEdge); }

        g.setColour (fill);
        g.fillRoundedRectangle (r, corner);
        // A restrained top edge gives controls the same optical separation as
        // macOS dark materials without introducing a glossy bevel.
        if (! on)
        {
            g.setColour (juce::Colour (kTextPrimary).withAlpha (
                shouldDrawHighlighted ? 0.075f : 0.035f));
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

        const float fs = juce::jlimit (9.0f, 14.0f, (float) b.getHeight() * 0.5f);
        g.setFont (juce::FontOptions (fs, juce::Font::plain));
        g.setColour (on ? getAccent().brighter (0.25f)
                        : juce::Colour (kTextPrimary).withMultipliedAlpha (b.isEnabled() ? (over || down ? 1.0f : 0.92f) : 0.4f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (8, 0), juce::Justification::centred, 1);
    }

private:
    // Slim color-coded handle: a flat block in the control's accent with one bright
    // leading edge, so it reads as a precise handle rather than a stray white bar.
    static void drawFaderCap (juce::Graphics& g, juce::Rectangle<float> cap, juce::Colour col)
    {
        g.setColour (col);
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (juce::Colour (stemdeck::tokens::fgPrimary).withAlpha (0.92f));
        if (cap.getWidth() >= cap.getHeight())   // horizontal handle → bright top edge
            g.fillRect (cap.getX() + 1.5f, cap.getY() + 1.0f, cap.getWidth() - 3.0f, 1.3f);
        else                                     // vertical handle → bright left edge
            g.fillRect (cap.getX() + 1.0f, cap.getY() + 1.5f, 1.3f, cap.getHeight() - 3.0f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemDeckLookAndFeel)
};
