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
inline const juce::Colour overlay         { 0xff17191e };
inline const juce::Colour overlayHigh     { 0xff22252c };
inline const juce::Colour overlayEdge     { 0xff3a3e48 };
inline const juce::Colour overlayText     { 0xfff5f6f8 };

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

inline void drawFocusRing (juce::Graphics& g, juce::Rectangle<float> bounds,
                           float radius, float alpha = 0.72f)
{
    setSpectrumGradient (g, bounds, alpha);
    g.drawRoundedRectangle (bounds.reduced (0.75f), radius, 1.5f);
}

inline void fillPorcelain (juce::Graphics& g, juce::Rectangle<float> bounds,
                           float radius, bool pressed = false)
{
    const float travel = pressed ? 0.8f : 2.0f;

    // Layered ambient/contact shadows read more naturally than a single dark
    // duplicate and remain crisp at small plugin sizes.
    g.setColour (juce::Colours::black.withAlpha (pressed ? 0.035f : 0.055f));
    g.fillRoundedRectangle (bounds.expanded (3.0f).translated (0.0f, travel + 2.0f),
                            radius + 3.0f);
    g.setColour (juce::Colours::black.withAlpha (pressed ? 0.075f : 0.105f));
    g.fillRoundedRectangle (bounds.expanded (1.25f).translated (0.0f, travel + 0.8f),
                            radius + 1.25f);
    g.setColour (juce::Colours::black.withAlpha (pressed ? 0.10f : 0.14f));
    g.fillRoundedRectangle (bounds.translated (0.0f, travel), radius);

    if (pressed)
    {
        g.setGradientFill (juce::ColourGradient (
            porcelainPress, bounds.getX(), bounds.getY(),
            porcelainBottom.darker (0.035f), bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, radius);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (
            porcelainTop, bounds.getX(), bounds.getY(),
            porcelainBottom, bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, radius);
    }

    g.setColour (juce::Colours::white.withAlpha (pressed ? 0.42f : 0.88f));
    g.drawRoundedRectangle (bounds.reduced (1.1f), juce::jmax (1.0f, radius - 1.0f), 1.0f);
    g.setColour (porcelainEdge.withAlpha (pressed ? 0.84f : 0.72f));
    g.drawRoundedRectangle (bounds.reduced (0.25f), radius, 1.0f);

    // A restrained specular edge gives the surface the optical precision of
    // machined glass without turning it glossy.
    auto highlight = bounds.reduced (radius * 0.32f, 1.5f).removeFromTop (1.0f);
    g.setColour (juce::Colours::white.withAlpha (pressed ? 0.26f : 0.70f));
    g.fillRoundedRectangle (highlight, 0.5f);
}

inline void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, juce::Slider::SliderStyle style,
                              bool enabled, bool hover, bool down, bool focused)
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
        g.setColour (juce::Colour (0xffc5c8cd));
        g.fillRoundedRectangle (groove, groove.getWidth() * 0.5f);

        const float valueY = juce::jlimit (groove.getY(), groove.getBottom(), sliderPos);
        auto value = groove.withTop (valueY);
        setSpectrumGradient (g, groove, enabled ? (hover ? 0.94f : 0.78f) : 0.28f);
        g.fillRoundedRectangle (value, groove.getWidth() * 0.5f);

        auto thumb = juce::Rectangle<float> (18.0f, 18.0f)
                         .withCentre ({ area.getCentreX(), valueY });
        fillPorcelain (g, thumb, 9.0f, down);
        if (focused)
            drawFocusRing (g, thumb.expanded (2.0f), 11.0f);
        return;
    }

    auto shell = juce::Rectangle<float> (area.getX() + 3.0f, area.getCentreY() - 7.0f,
                                          area.getWidth() - 6.0f, 14.0f);
    fillPorcelain (g, shell, 7.0f);

    auto groove = shell.reduced (3.0f);
    g.setColour (juce::Colour (0xffc5c8cd));
    g.fillRoundedRectangle (groove, groove.getHeight() * 0.5f);

    const float valueX = juce::jlimit (groove.getX(), groove.getRight(), sliderPos);
    auto value = groove.withRight (valueX);
    setSpectrumGradient (g, groove, enabled ? (hover ? 0.94f : 0.78f) : 0.28f);
    g.fillRoundedRectangle (value, groove.getHeight() * 0.5f);

    auto thumb = juce::Rectangle<float> (18.0f, 18.0f)
                     .withCentre ({ valueX, area.getCentreY() });
    fillPorcelain (g, thumb, 9.0f, down);
    g.setColour (ink.withAlpha (enabled ? 0.18f : 0.08f));
    g.fillEllipse (thumb.reduced (5.0f));
    if (focused)
        drawFocusRing (g, thumb.expanded (2.0f), 11.0f);
}

inline void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float startAngle, float endAngle,
                              bool enabled, bool hover, bool down, bool focused)
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
    setSpectrumGradient (g, area, enabled ? (hover ? 0.98f : 0.82f) : 0.28f);
    g.strokePath (value, juce::PathStrokeType (
        lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (radius > 24.0f)
    {
        setSpectrumGradient (g, area, enabled ? (hover ? 0.10f : 0.055f) : 0.02f);
        g.drawEllipse (juce::Rectangle<float> (radius * 1.76f, radius * 1.76f)
                           .withCentre (centre), lineWidth * 1.75f);
    }

    auto knob = juce::Rectangle<float> (radius * 1.52f, radius * 1.52f)
                    .withCentre (centre);
    fillPorcelain (g, knob, knob.getWidth() * 0.5f, down);

    g.setGradientFill (juce::ColourGradient (
        juce::Colours::white.withAlpha (enabled ? 0.94f : 0.48f),
        knob.getX() + knob.getWidth() * 0.30f, knob.getY() + knob.getHeight() * 0.24f,
        juce::Colour (0xffbfc3c9).withAlpha (enabled ? 0.96f : 0.50f),
        knob.getRight(), knob.getBottom(), true));
    g.fillEllipse (knob.reduced (3.0f));
    g.setColour (juce::Colours::white.withAlpha (enabled ? 0.72f : 0.32f));
    g.drawEllipse (knob.reduced (4.0f), 1.0f);
    g.setColour (porcelainEdge.withAlpha (0.84f));
    g.drawEllipse (knob.reduced (2.5f), 1.0f);

    const auto direction = juce::Point<float> (
        std::cos (angle - juce::MathConstants<float>::halfPi),
        std::sin (angle - juce::MathConstants<float>::halfPi));
    g.setColour (ink.withAlpha (enabled ? 0.64f : 0.28f));
    g.drawLine ({ centre + direction * (radius * 0.22f),
                  centre + direction * (radius * 0.55f) },
                juce::jmax (1.3f, radius * 0.045f));

    if (focused)
    {
        setSpectrumGradient (g, knob, 0.74f);
        g.drawEllipse (knob.expanded (2.0f), 1.5f);
    }
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

    if (button.hasKeyboardFocus (true))
        drawFocusRing (g, bounds, bounds.getHeight() * 0.5f);
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
                              bool hover, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const float trackHeight = 20.0f;
    auto track = juce::Rectangle<float> (
        bounds.getX(), bounds.getCentreY() - trackHeight * 0.5f, 38.0f, trackHeight);
    fillPorcelain (g, track, trackHeight * 0.5f, down);

    const bool on = button.getToggleState();
    if (on)
    {
        setSpectrumGradient (g, track, button.isEnabled() ? (hover ? 0.90f : 0.76f) : 0.28f);
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
    fillPorcelain (g, knob, diameter * 0.5f, down);

    if (button.hasKeyboardFocus (true))
        drawFocusRing (g, track.expanded (2.0f), trackHeight * 0.5f + 2.0f);

    g.setColour (button.isEnabled() ? (on ? juce::Colours::white : juce::Colour (0xffc8ccd1))
                                     : juce::Colour (0xffa3a7ad));
    g.setFont (sattari::brand::font (11.5f));
    g.drawText (button.getButtonText(),
                bounds.withTrimmedLeft (47.0f).toNearestInt(),
                juce::Justification::centredLeft, true);
}

inline void drawScrollbar (juce::Graphics& g, int x, int y, int width, int height,
                           bool vertical, int thumbStart, int thumbSize,
                           bool hover, bool down)
{
    auto lane = juce::Rectangle<float> ((float) x, (float) y,
                                        (float) width, (float) height).reduced (2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    g.fillRoundedRectangle (lane, juce::jmin (4.0f, lane.getWidth() * 0.5f));

    juce::Rectangle<float> thumb;
    if (vertical)
        thumb = { (float) x + 2.0f, (float) thumbStart, (float) width - 4.0f, (float) thumbSize };
    else
        thumb = { (float) thumbStart, (float) y + 2.0f, (float) thumbSize, (float) height - 4.0f };

    g.setColour (juce::Colours::white.withAlpha (down ? 0.56f : (hover ? 0.42f : 0.25f)));
    g.fillRoundedRectangle (thumb, juce::jmin (4.0f, juce::jmin (thumb.getWidth(), thumb.getHeight()) * 0.5f));
}

inline void fillTextEditorBackground (juce::Graphics& g, int width, int height)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                      .reduced (0.5f);
    g.setGradientFill (juce::ColourGradient (
        overlayHigh, bounds.getX(), bounds.getY(),
        overlay, bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, juce::jmin (9.0f, bounds.getHeight() * 0.32f));
}

inline void drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                   bool enabled, bool focused, bool readOnly)
{
    if (! enabled)
        return;

    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                      .reduced (0.5f);
    const auto radius = juce::jmin (9.0f, bounds.getHeight() * 0.32f);
    if (focused && ! readOnly)
        drawFocusRing (g, bounds, radius, 0.74f);
    else
    {
        g.setColour (overlayEdge.withAlpha (0.86f));
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }
}

inline void drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                      .reduced (0.5f);
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 1.0f), 11.0f);
    g.setGradientFill (juce::ColourGradient (
        overlayHigh.brighter (0.025f), bounds.getX(), bounds.getY(),
        overlay, bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 11.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.5f, 1.0f);
}

inline void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                               bool separator, bool active, bool highlighted,
                               bool ticked, bool hasSubMenu, const juce::String& text,
                               const juce::String& shortcut, const juce::Drawable* icon,
                               const juce::Colour* requestedTextColour,
                               juce::Font font, const juce::Path& tickShape)
{
    if (separator)
    {
        auto line = area.reduced (12, 0).withHeight (1).withCentre (area.getCentre());
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (line);
        return;
    }

    auto row = area.reduced (5, 2);
    if (highlighted && active)
    {
        setSpectrumGradient (g, row.toFloat(), 0.16f);
        g.fillRoundedRectangle (row.toFloat(), 7.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (row.toFloat().reduced (0.5f), 6.5f, 1.0f);
    }

    auto content = row.reduced (7, 0);
    const auto textColour = requestedTextColour != nullptr ? *requestedTextColour : overlayText;
    g.setColour (textColour.withMultipliedAlpha (active ? 1.0f : 0.38f));
    font.setHeight (juce::jmin (font.getHeight(), (float) content.getHeight() * 0.52f));
    g.setFont (font);

    auto iconArea = content.removeFromLeft (juce::jmin (20, content.getHeight())).toFloat();
    if (icon != nullptr)
    {
        icon->drawWithin (g, iconArea.reduced (2.0f),
                          juce::RectanglePlacement::centred
                              | juce::RectanglePlacement::onlyReduceInSize,
                          active ? 1.0f : 0.38f);
    }
    else if (ticked)
    {
        g.fillPath (tickShape, tickShape.getTransformToScaleToFit (
            iconArea.reduced (4.0f), true));
    }

    content.removeFromLeft (6);
    if (hasSubMenu)
    {
        auto arrowArea = content.removeFromRight (12).toFloat();
        juce::Path arrow;
        arrow.startNewSubPath (arrowArea.getX() + 3.0f, arrowArea.getCentreY() - 3.5f);
        arrow.lineTo (arrowArea.getX() + 7.0f, arrowArea.getCentreY());
        arrow.lineTo (arrowArea.getX() + 3.0f, arrowArea.getCentreY() + 3.5f);
        g.strokePath (arrow, juce::PathStrokeType (
            1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.drawFittedText (text, content, juce::Justification::centredLeft, 1);
    if (shortcut.isNotEmpty())
    {
        g.setColour (textColour.withMultipliedAlpha (active ? 0.52f : 0.24f));
        g.setFont (sattari::brand::font (font.getHeight() * 0.86f));
        g.drawText (shortcut, content, juce::Justification::centredRight, true);
    }
}

inline void drawTooltip (juce::Graphics& g, const juce::String& text,
                         int width, int height)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                      .reduced (0.5f);
    g.setColour (juce::Colours::black.withAlpha (0.24f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 1.0f), 8.0f);
    g.setColour (overlayHigh.withAlpha (0.98f));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
    g.setColour (overlayText);
    g.setFont (sattari::brand::font (12.0f));
    g.drawFittedText (text, bounds.reduced (8.0f, 5.0f).toNearestInt(),
                      juce::Justification::centredLeft, 4);
}

} // namespace sattari::control
