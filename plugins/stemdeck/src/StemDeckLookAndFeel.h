#pragma once

// =============================================================================
//  StemDeck — flat, minimal, robust LookAndFeel
// -----------------------------------------------------------------------------
//  djay-Pro feel × the Sattari auto-tune aesthetic: flat surfaces, hairline
//  borders, one strong accent, clean bold-ish type. No gradients, glows, or
//  drop shadows — every surface is a solid fill. Scoped to StemDeck so the rest
//  of the suite is untouched. It only adds what the base look lacks (slim flat
//  faders) and firms up the knobs/buttons; nothing here decorates.
// =============================================================================

#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariBranding.h>

class StemDeckLookAndFeel : public sattari::ui::SattariLookAndFeel
{
public:
    explicit StemDeckLookAndFeel (juce::uint32 accentArgb = sattari::brand::kAccent)
        : sattari::ui::SattariLookAndFeel (accentArgb)
    {
        setColour (juce::Slider::trackColourId, getAccent());
    }

    // Flat backdrop: solid fill + hairline frame + a single thin accent line.
    void paintModernBackground (juce::Graphics& g, juce::Rectangle<int> bounds) const
    {
        using namespace sattari::brand;
        g.fillAll (juce::Colour (kBackground));
        auto frame = bounds.toFloat().reduced (kPanelInset);
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
        const auto lineW  = juce::jmax (2.5f, radius * 0.10f);
        const auto arcR   = radius - lineW;

        auto fill = slider.findColour (juce::Slider::rotarySliderFillColourId);
        if (fill.isTransparent()) fill = getAccent();

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (kHairline));
        g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (fill);
        g.strokePath (arc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const juce::Point<float> tip (centre.x + arcR * std::cos (angle - juce::MathConstants<float>::halfPi),
                                      centre.y + arcR * std::sin (angle - juce::MathConstants<float>::halfPi));
        g.setColour (juce::Colour (kTextPrimary));
        g.fillEllipse (juce::Rectangle<float> (lineW * 1.6f, lineW * 1.6f).withCentre (tip));
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
            const float trackW = 5.0f;
            auto track = juce::Rectangle<float> (cx - trackW * 0.5f, bounds.getY() + 4.0f, trackW, bounds.getHeight() - 8.0f);
            g.setColour (juce::Colour (kPanelDeep));  g.fillRoundedRectangle (track, trackW * 0.5f);
            g.setColour (juce::Colour (kHairline));   g.drawRoundedRectangle (track, trackW * 0.5f, 1.0f);

            const float top = juce::jlimit (track.getY(), track.getBottom(), sliderPos);
            g.setColour (col);
            g.fillRoundedRectangle (juce::Rectangle<float> (track.getX(), top, trackW, track.getBottom() - top), trackW * 0.5f);

            drawFlatThumb (g, juce::Rectangle<float> (18.0f, 9.0f).withCentre ({ cx, top }), col);
        }
        else
        {
            const float cy = bounds.getCentreY();
            const float trackH = 5.0f;
            auto track = juce::Rectangle<float> (bounds.getX() + 4.0f, cy - trackH * 0.5f, bounds.getWidth() - 8.0f, trackH);
            g.setColour (juce::Colour (kPanelDeep));  g.fillRoundedRectangle (track, trackH * 0.5f);
            g.setColour (juce::Colour (kHairline));   g.drawRoundedRectangle (track, trackH * 0.5f, 1.0f);

            const float right = juce::jlimit (track.getX(), track.getRight(), sliderPos);
            g.setColour (col);
            g.fillRoundedRectangle (juce::Rectangle<float> (track.getX(), track.getY(), right - track.getX(), trackH), trackH * 0.5f);

            drawFlatThumb (g, juce::Rectangle<float> (9.0f, 18.0f).withCentre ({ right, cy }), col);
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool shouldDrawHighlighted, bool shouldDrawDown) override
    {
        using namespace sattari::brand;
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();

        juce::Colour fill = juce::Colour (kPanelRaised);
        if (shouldDrawDown)             fill = getAccent().withAlpha (0.28f);
        else if (on)                    fill = getAccent().withAlpha (0.22f);
        else if (shouldDrawHighlighted) fill = juce::Colour (kPanelRaised).brighter (0.10f);

        g.setColour (fill);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour ((on || shouldDrawDown || shouldDrawHighlighted) ? getAccent().withAlpha (0.85f)
                                                                     : juce::Colour (kHairline));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

private:
    static void drawFlatThumb (juce::Graphics& g, juce::Rectangle<float> thumb, juce::Colour col)
    {
        using namespace sattari::brand;
        g.setColour (juce::Colour (kTextPrimary));
        g.fillRoundedRectangle (thumb, 3.0f);
        g.setColour (col);
        g.fillRoundedRectangle (thumb.reduced (thumb.getWidth() * 0.32f, thumb.getHeight() * 0.32f), 1.5f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemDeckLookAndFeel)
};
