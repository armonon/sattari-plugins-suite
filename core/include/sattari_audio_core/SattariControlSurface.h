#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <sattari_audio_core/SattariBranding.h>

#include <cmath>

namespace sattari::control
{

inline const juce::Colour porcelainTop    { 0xfffbfbfc };
inline const juce::Colour porcelainBottom { 0xffdfe1e4 };
inline const juce::Colour porcelainEdge   { 0xffbfc3c8 };
inline const juce::Colour porcelainPress  { 0xffcfd3d8 };
inline const juce::Colour ink             { 0xff191a1d };
inline const juce::Colour inkMuted        { 0xff5f646b };
inline const juce::Colour cyan            { 0xff19b9e8 };
inline const juce::Colour blue            { 0xff5291ff };
inline const juce::Colour violet          { 0xff9d86ff };
inline const juce::Colour blush           { 0xffffb8c5 };
inline const juce::Colour warm            { 0xffffe6a4 };

inline void setSpectrumGradient (juce::Graphics& g, juce::Rectangle<float> bounds,
                                 float alpha = 1.0f)
{
    juce::ColourGradient spectrum (
        cyan.withAlpha (alpha), bounds.getX(), bounds.getCentreY(),
        warm.withAlpha (alpha), bounds.getRight(), bounds.getCentreY(), false);
    spectrum.addColour (0.32, blue.withAlpha (alpha));
    spectrum.addColour (0.55, violet.withAlpha (alpha));
    spectrum.addColour (0.76, blush.withAlpha (alpha));
    g.setGradientFill (spectrum);
}

inline void fillPorcelain (juce::Graphics& g, juce::Rectangle<float> bounds,
                           float radius, bool pressed = false)
{
    g.setColour (juce::Colours::black.withAlpha (pressed ? 0.22f : 0.28f));
    g.fillRoundedRectangle (bounds.translated (0.0f, pressed ? 1.0f : 2.0f), radius);

    if (pressed)
    {
        g.setColour (porcelainPress);
        g.fillRoundedRectangle (bounds, radius);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (
            porcelainTop, bounds.getX(), bounds.getY(),
            porcelainBottom, bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, radius);
    }

    g.setColour (juce::Colours::white.withAlpha (0.82f));
    g.drawRoundedRectangle (bounds.reduced (0.75f), radius, 1.0f);
    g.setColour (porcelainEdge.withAlpha (0.78f));
    g.drawRoundedRectangle (bounds.reduced (0.25f), radius, 1.0f);
}

inline void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, juce::Slider::SliderStyle style,
                              bool enabled)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y,
                                               (float) width, (float) height);
    const bool vertical = style == juce::Slider::LinearVertical
                       || style == juce::Slider::LinearBarVertical;

    if (vertical)
    {
        auto track = juce::Rectangle<float> (area.getCentreX() - 6.0f, area.getY() + 5.0f,
                                              12.0f, area.getHeight() - 10.0f);
        fillPorcelain (g, track, 6.0f);
        auto groove = track.reduced (3.0f);
        g.setColour (juce::Colour (0xffc9ccd0));
        g.fillRoundedRectangle (groove, groove.getWidth() * 0.5f);

        const float valueY = juce::jlimit (groove.getY(), groove.getBottom(), sliderPos);
        auto value = groove.withTop (valueY);
        setSpectrumGradient (g, groove, enabled ? 1.0f : 0.36f);
        g.fillRoundedRectangle (value, groove.getWidth() * 0.5f);

        auto thumb = juce::Rectangle<float> (18.0f, 18.0f)
                         .withCentre ({ area.getCentreX(), valueY });
        fillPorcelain (g, thumb, 9.0f);
        return;
    }

    auto shell = juce::Rectangle<float> (area.getX() + 3.0f, area.getCentreY() - 7.0f,
                                          area.getWidth() - 6.0f, 14.0f);
    fillPorcelain (g, shell, 7.0f);

    auto groove = shell.reduced (3.0f);
    g.setColour (juce::Colour (0xffc9ccd0));
    g.fillRoundedRectangle (groove, groove.getHeight() * 0.5f);

    const float valueX = juce::jlimit (groove.getX(), groove.getRight(), sliderPos);
    auto value = groove.withRight (valueX);
    setSpectrumGradient (g, groove, enabled ? 1.0f : 0.36f);
    g.fillRoundedRectangle (value, groove.getHeight() * 0.5f);

    auto thumb = juce::Rectangle<float> (18.0f, 18.0f)
                     .withCentre ({ valueX, area.getCentreY() });
    fillPorcelain (g, thumb, 9.0f);
    g.setColour (ink.withAlpha (enabled ? 0.18f : 0.08f));
    g.fillEllipse (thumb.reduced (5.0f));
}

inline void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float startAngle, float endAngle,
                              bool enabled)
{
    auto area = juce::Rectangle<float> ((float) x, (float) y,
                                        (float) width, (float) height).reduced (7.0f);
    const float radius = juce::jmax (8.0f, juce::jmin (area.getWidth(), area.getHeight()) * 0.5f);
    const auto centre = area.getCentre();
    const float arcRadius = radius - 3.5f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float lineWidth = juce::jlimit (2.0f, 4.5f, radius * 0.085f);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         startAngle, endAngle, true);
    g.setColour (porcelainEdge.withAlpha (enabled ? 0.66f : 0.28f));
    g.strokePath (track, juce::PathStrokeType (
        lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         startAngle, angle, true);
    setSpectrumGradient (g, area, enabled ? 1.0f : 0.32f);
    g.strokePath (value, juce::PathStrokeType (
        lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (radius > 24.0f)
    {
        g.setColour (warm.withAlpha (enabled ? 0.10f : 0.03f));
        g.drawEllipse (juce::Rectangle<float> (radius * 1.72f, radius * 1.72f)
                           .withCentre (centre), lineWidth * 2.1f);
    }

    auto knob = juce::Rectangle<float> (radius * 1.52f, radius * 1.52f)
                    .withCentre (centre);
    fillPorcelain (g, knob, knob.getWidth() * 0.5f);

    g.setGradientFill (juce::ColourGradient (
        juce::Colours::white.withAlpha (enabled ? 0.82f : 0.42f),
        knob.getX(), knob.getY(),
        juce::Colour (0xffc7cbd0).withAlpha (enabled ? 0.96f : 0.48f),
        knob.getRight(), knob.getBottom(), false));
    g.fillEllipse (knob.reduced (3.0f));
    g.setColour (porcelainEdge.withAlpha (0.9f));
    g.drawEllipse (knob.reduced (2.5f), 1.0f);

    const auto direction = juce::Point<float> (
        std::cos (angle - juce::MathConstants<float>::halfPi),
        std::sin (angle - juce::MathConstants<float>::halfPi));
    g.setColour (ink.withAlpha (enabled ? 0.64f : 0.28f));
    g.drawLine ({ centre + direction * (radius * 0.22f),
                  centre + direction * (radius * 0.55f) },
                juce::jmax (1.3f, radius * 0.045f));
}

inline void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                  bool hover, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    fillPorcelain (g, bounds, bounds.getHeight() * 0.5f, down);

    if (button.getToggleState())
    {
        setSpectrumGradient (g, bounds, button.isEnabled() ? 0.24f : 0.10f);
        g.fillRoundedRectangle (bounds.reduced (2.0f), bounds.getHeight() * 0.5f - 2.0f);
    }
    else if (hover && button.isEnabled())
    {
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillRoundedRectangle (bounds.reduced (2.0f), bounds.getHeight() * 0.5f - 2.0f);
    }
}

inline void drawComboBox (juce::Graphics& g, int width, int height, bool down,
                          bool focused)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                      .reduced (1.0f);
    fillPorcelain (g, bounds, bounds.getHeight() * 0.5f, down);
    if (focused)
    {
        setSpectrumGradient (g, bounds, 0.7f);
        g.drawRoundedRectangle (bounds.reduced (0.5f), bounds.getHeight() * 0.5f, 1.5f);
    }

    const float cx = (float) width - 16.0f;
    const float cy = (float) height * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (inkMuted);
    g.strokePath (arrow, juce::PathStrokeType (
        1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

inline void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                              bool hover)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const float trackHeight = 20.0f;
    auto track = juce::Rectangle<float> (
        bounds.getX(), bounds.getCentreY() - trackHeight * 0.5f, 38.0f, trackHeight);
    fillPorcelain (g, track, trackHeight * 0.5f);

    const bool on = button.getToggleState();
    if (on)
    {
        setSpectrumGradient (g, track, button.isEnabled() ? 0.88f : 0.30f);
        g.fillRoundedRectangle (track.reduced (2.0f), trackHeight * 0.5f - 2.0f);
    }
    else if (hover)
    {
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRoundedRectangle (track.reduced (2.0f), trackHeight * 0.5f - 2.0f);
    }

    const float diameter = 16.0f;
    const float knobX = on ? track.getRight() - diameter - 2.0f : track.getX() + 2.0f;
    auto knob = juce::Rectangle<float> (knobX, track.getY() + 2.0f, diameter, diameter);
    fillPorcelain (g, knob, diameter * 0.5f);

    g.setColour (button.isEnabled() ? (on ? juce::Colours::white : juce::Colour (0xffc8ccd1))
                                     : juce::Colour (0xffa3a7ad));
    g.setFont (sattari::brand::font (11.5f));
    g.drawText (button.getButtonText(),
                bounds.withTrimmedLeft (47.0f).toNearestInt(),
                juce::Justification::centredLeft, true);
}

} // namespace sattari::control
