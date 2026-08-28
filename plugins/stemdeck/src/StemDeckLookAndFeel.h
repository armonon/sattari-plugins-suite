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
namespace stemdeck::palette
{
    inline constexpr juce::uint32 kBackground  = 0xff0b0c0e;   // app ground (near-black)
    inline constexpr juce::uint32 kPanel       = 0xff101216;   // matte panel
    inline constexpr juce::uint32 kPanelRaised  = 0xff171a20;   // raised control / button
    inline constexpr juce::uint32 kPanelDeep    = 0xff08090b;   // recessed track well
    inline constexpr juce::uint32 kHairline     = 0xff262932;   // divider / border
    inline constexpr juce::uint32 kGlassEdge     = 0xff2f333c;   // brighter line (kept name for editor compat)
    inline constexpr juce::uint32 kTextPrimary  = 0xffedf0f4;   // near-white (slight cool)
    inline constexpr juce::uint32 kTextMuted     = 0xff828892;   // neutral grey
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
        // Clean geometric sans; scoped to StemDeck.
        setDefaultSansSerifTypefaceName ("Avenir Next");
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

        const float corner = 7.0f;

        juce::Colour fill = juce::Colour (kPanelRaised);
        if (shouldDrawDown)             fill = getAccent().withAlpha (0.28f);
        else if (on)                    fill = getAccent().withAlpha (0.16f);
        else if (shouldDrawHighlighted) fill = juce::Colour (kPanelRaised).brighter (0.10f);

        g.setColour (fill);
        g.fillRoundedRectangle (r, corner);

        g.setColour ((on || shouldDrawDown || shouldDrawHighlighted) ? getAccent().withAlpha (0.75f)
                                                                     : juce::Colour (kHairline));
        g.drawRoundedRectangle (r, corner, 1.0f);
    }

private:
    // Slim color-coded handle: a flat block in the control's accent with one bright
    // leading edge, so it reads as a precise handle rather than a stray white bar.
    static void drawFaderCap (juce::Graphics& g, juce::Rectangle<float> cap, juce::Colour col)
    {
        g.setColour (col);
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (juce::Colour (stemdeck::palette::kTextPrimary).withAlpha (0.92f));
        if (cap.getWidth() >= cap.getHeight())   // horizontal handle → bright top edge
            g.fillRect (cap.getX() + 1.5f, cap.getY() + 1.0f, cap.getWidth() - 3.0f, 1.3f);
        else                                     // vertical handle → bright left edge
            g.fillRect (cap.getX() + 1.0f, cap.getY() + 1.5f, 1.3f, cap.getHeight() - 3.0f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemDeckLookAndFeel)
};
