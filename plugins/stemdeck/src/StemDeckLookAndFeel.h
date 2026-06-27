#pragma once

// =============================================================================
//  StemDeck — ultra-modern LookAndFeel
// -----------------------------------------------------------------------------
//  A StemDeck-scoped subclass of the shared suite look. Keeps the brand palette
//  and all stable method names, but adds depth and glow for a modern DJ-console
//  feel: thicker glowing value arcs on knobs, slim faders with a glowing thumb,
//  gradient buttons with an accent ring when active, and a gradient backdrop
//  with a soft accent aura.  Scoped here so the other suite plugins are
//  untouched.
// =============================================================================

#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariBranding.h>

class StemDeckLookAndFeel : public sattari::ui::SattariLookAndFeel
{
public:
    explicit StemDeckLookAndFeel (juce::uint32 accentArgb = sattari::brand::kAccent)
        : sattari::ui::SattariLookAndFeel (accentArgb)
    {
        // Register a sane fader track colour so sliders that don't override it
        // still read in the accent.
        setColour (juce::Slider::trackColourId, getAccent());
    }

    // Modern gradient backdrop with a faint accent aura along the top.
    void paintModernBackground (juce::Graphics& g, juce::Rectangle<int> bounds) const
    {
        using namespace sattari::brand;
        auto r = bounds.toFloat();

        juce::ColourGradient base (juce::Colour (kBackground).brighter (0.05f), r.getCentreX(), r.getY(),
                                   juce::Colour (kBackground),                  r.getCentreX(), r.getBottom(), false);
        g.setGradientFill (base);
        g.fillRect (r);

        juce::ColourGradient aura (getAccent().withAlpha (0.05f), r.getCentreX(), r.getY(),
                                   juce::Colours::transparentBlack, r.getCentreX(), r.getY() + 160.0f, false);
        g.setGradientFill (aura);
        g.fillRect (r.withHeight (160.0f));

        auto frame = r.reduced (kPanelInset);
        g.setColour (juce::Colour (kHairline));
        g.drawRoundedRectangle (frame, kCornerRadius, 1.0f);
        auto top = frame.removeFromTop (2.0f).reduced (kCornerRadius, 0.0f);
        g.setColour (getAccent().withAlpha (0.6f));
        g.fillRoundedRectangle (top, 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        using namespace sattari::brand;
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle  = startAngle + sliderPos * (endAngle - startAngle);
        const auto lineW  = juce::jmax (2.5f, radius * 0.11f);
        const auto arcR   = radius - lineW;

        auto fill = slider.findColour (juce::Slider::rotarySliderFillColourId);
        if (fill.isTransparent()) fill = getAccent();

        // Inset disc for depth.
        g.setColour (juce::Colour (kPanelDeep));
        g.fillEllipse (juce::Rectangle<float> (arcR * 2.0f, arcR * 2.0f).withCentre (centre));

        // Track.
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (kHairline));
        g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Glow + crisp value arc.
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (fill.withAlpha (0.25f));
        g.strokePath (arc, juce::PathStrokeType (lineW * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (fill);
        g.strokePath (arc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Indicator with a soft halo.
        const juce::Point<float> tip (centre.x + arcR * std::cos (angle - juce::MathConstants<float>::halfPi),
                                      centre.y + arcR * std::sin (angle - juce::MathConstants<float>::halfPi));
        g.setColour (fill.withAlpha (0.30f));
        g.fillEllipse (juce::Rectangle<float> (lineW * 3.2f, lineW * 3.2f).withCentre (tip));
        g.setColour (juce::Colour (kTextPrimary));
        g.fillEllipse (juce::Rectangle<float> (lineW * 1.7f, lineW * 1.7f).withCentre (tip));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        using namespace sattari::brand;
        auto col = slider.findColour (juce::Slider::trackColourId);
        if (col.isTransparent()) col = getAccent();
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();

        const bool vertical = style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical;
        if (vertical)
        {
            const float cx = bounds.getCentreX();
            const float trackW = 6.0f;
            auto track = juce::Rectangle<float> (cx - trackW * 0.5f, bounds.getY() + 4.0f, trackW, bounds.getHeight() - 8.0f);
            g.setColour (juce::Colour (kPanelDeep));   g.fillRoundedRectangle (track, trackW * 0.5f);
            g.setColour (juce::Colour (kHairline));    g.drawRoundedRectangle (track, trackW * 0.5f, 1.0f);

            const float top = juce::jlimit (track.getY(), track.getBottom(), sliderPos);
            auto fill = juce::Rectangle<float> (track.getX(), top, trackW, track.getBottom() - top);
            g.setColour (col.withAlpha (0.25f)); g.fillRoundedRectangle (fill.expanded (2.5f, 0.0f), trackW);
            g.setColour (col);                   g.fillRoundedRectangle (fill, trackW * 0.5f);

            drawFaderThumb (g, juce::Rectangle<float> (20.0f, 10.0f).withCentre ({ cx, top }), col);
        }
        else
        {
            const float cy = bounds.getCentreY();
            const float trackH = 6.0f;
            auto track = juce::Rectangle<float> (bounds.getX() + 4.0f, cy - trackH * 0.5f, bounds.getWidth() - 8.0f, trackH);
            g.setColour (juce::Colour (kPanelDeep));   g.fillRoundedRectangle (track, trackH * 0.5f);
            g.setColour (juce::Colour (kHairline));    g.drawRoundedRectangle (track, trackH * 0.5f, 1.0f);

            const float right = juce::jlimit (track.getX(), track.getRight(), sliderPos);
            auto fill = juce::Rectangle<float> (track.getX(), track.getY(), right - track.getX(), trackH);
            g.setColour (col.withAlpha (0.25f)); g.fillRoundedRectangle (fill.expanded (0.0f, 2.5f), trackH);
            g.setColour (col);                   g.fillRoundedRectangle (fill, trackH * 0.5f);

            drawFaderThumb (g, juce::Rectangle<float> (10.0f, 20.0f).withCentre ({ right, cy }), col);
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool shouldDrawHighlighted, bool shouldDrawDown) override
    {
        using namespace sattari::brand;
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();

        juce::Colour topCol = juce::Colour (kPanelRaised).brighter (0.10f);
        juce::Colour botCol = juce::Colour (kPanelRaised);
        if (shouldDrawDown)        { topCol = getAccent().withAlpha (0.28f); botCol = getAccent().withAlpha (0.18f); }
        else if (on)               { topCol = getAccent().withAlpha (0.34f); botCol = getAccent().withAlpha (0.20f); }
        else if (shouldDrawHighlighted) { topCol = topCol.brighter (0.10f); }

        juce::ColourGradient grad (topCol, r.getCentreX(), r.getY(), botCol, r.getCentreX(), r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, 7.0f);

        const bool active = on || shouldDrawDown || shouldDrawHighlighted;
        if (active)
        {
            g.setColour (getAccent().withAlpha (0.22f));
            g.drawRoundedRectangle (r.expanded (1.0f), 8.0f, 1.5f);   // soft outer ring
        }
        g.setColour (active ? getAccent().withAlpha (0.85f) : juce::Colour (kHairline));
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
    }

private:
    static void drawFaderThumb (juce::Graphics& g, juce::Rectangle<float> thumb, juce::Colour col)
    {
        using namespace sattari::brand;
        g.setColour (col.withAlpha (0.30f));
        g.fillRoundedRectangle (thumb.expanded (2.5f), 5.0f);            // glow
        g.setColour (juce::Colour (kTextPrimary));
        g.fillRoundedRectangle (thumb, 4.0f);                            // cap
        g.setColour (col);
        g.fillRoundedRectangle (thumb.reduced (thumb.getWidth() * 0.30f, thumb.getHeight() * 0.30f), 2.0f);  // accent core
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemDeckLookAndFeel)
};
