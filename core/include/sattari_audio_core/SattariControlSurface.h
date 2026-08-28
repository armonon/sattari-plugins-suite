#pragma once

// =============================================================================
//  Sattari control surface — the suite's control vocabulary
// -----------------------------------------------------------------------------
//  Every control every plugin draws comes through here. The two LookAndFeels
//  (SattariLookAndFeel, sattari::studio::LookAndFeel) are the ONLY callers, so
//  this file is where the suite's look actually lives; changing it changes 49
//  editors at once.
//
//  THE GRAMMAR
//   * Capsule geometry. Tracks, buttons, switches and fields are fully rounded
//     (radius = height/2), not softened rectangles. This is the single trait
//     that carries the look — a 9px radius on a 20px control reads as a
//     different product entirely.
//   * Porcelain for anything you GRAB. Bright, physical, one soft shadow.
//   * A single-hue accent gradient for anything you READ AS A VALUE. Never a
//     rainbow: value fills that shift hue along their length make two different
//     settings look like two different colours, which is a lie about the data.
//   * The thumb rides INSIDE the track, it does not straddle it. That is what
//     lets a slider stay a clean capsule at any size.
//
//  THE ACCENT IS A PARAMETER, NOT A CONSTANT. Products carry their own accent
//  (mint / violet / amber / blue / crimson, see SattariBranding) and the whole
//  point of the identity system is that a Create slot looks different from a
//  Mix slot. Every function here therefore takes the accent, defaulted so that
//  a caller which does not care still gets the suite default.
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include <sattari_audio_core/SattariBranding.h>

#include <cmath>

namespace sattari::control
{

// --- Porcelain: the material for things you grab ------------------------------
inline const juce::Colour porcelainTop    { 0xfffdfdfe };
inline const juce::Colour porcelainBottom { 0xffe4e6ea };
inline const juce::Colour porcelainEdge   { 0xffc2c6cc };
inline const juce::Colour porcelainPress  { 0xffd2d6db };
inline const juce::Colour ink             { 0xff1b1c20 };
inline const juce::Colour inkMuted        { 0xff62676e };

// --- Recessed: the material for things you read --------------------------------
//  Lifted off pure black. The reference sits on charcoal, not ink: at plugin
//  sizes a near-black canvas makes every shadow disappear and the controls stop
//  reading as physical.
inline const juce::Colour overlay         { 0xff232429 };
inline const juce::Colour overlayHigh     { 0xff2c2e34 };
inline const juce::Colour overlayEdge     { 0xff3d404a };
inline const juce::Colour overlayText     { 0xfff5f6f8 };
inline const juce::Colour trough          { 0xffd7dade };   // empty part of a value track

// Legacy spectrum stops. Kept because real spectrum displays legitimately want a
// hue ramp; NO control uses them any more.
inline const juce::Colour cyan            { 0xff19b9e8 };
inline const juce::Colour blue            { 0xff5291ff };
inline const juce::Colour violet          { 0xff9d86ff };
inline const juce::Colour blush           { 0xffffb8c5 };
inline const juce::Colour warm            { 0xffffe6a4 };

inline juce::Colour defaultAccent() { return juce::Colour (sattari::brand::kAccent); }

// The two stops of a value fill. Derived rather than hand-picked so every product
// accent gets the same depth without a second colour needing to be authored.
inline juce::Colour accentDeep (juce::Colour accent)
{
    return accent.withMultipliedSaturation (1.12f).withMultipliedBrightness (0.82f);
}
inline juce::Colour accentLift (juce::Colour accent)
{
    return accent.withRotatedHue (0.022f).withMultipliedSaturation (0.90f)
                 .withMultipliedBrightness (1.16f);
}

// `along` runs deep -> lift in the direction the value grows, so a fuller control
// reads brighter. Vertical tracks fill upward, hence the flip.
inline void setAccentGradient (juce::Graphics& g, juce::Rectangle<float> bounds,
                               juce::Colour accent, float alpha = 1.0f,
                               bool vertical = false)
{
    const auto a = accentDeep (accent).withAlpha (alpha);
    const auto b = accentLift (accent).withAlpha (alpha);
    g.setGradientFill (vertical
        ? juce::ColourGradient (b, bounds.getCentreX(), bounds.getY(),
                                a, bounds.getCentreX(), bounds.getBottom(), false)
        : juce::ColourGradient (a, bounds.getX(), bounds.getCentreY(),
                                b, bounds.getRight(), bounds.getCentreY(), false));
}

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
                           float radius, juce::Colour accent = defaultAccent(),
                           float alpha = 0.95f)
{
    g.setColour (accentLift (accent).withAlpha (alpha));
    g.drawRoundedRectangle (bounds.reduced (0.75f), radius, 1.6f);
}

// One ambient + one contact shadow. The previous three-layer stack turned to mud
// once controls became capsules, because the expanded copies of a fully-rounded
// shape overlap far more than they do on a 9px radius.
inline void fillPorcelain (juce::Graphics& g, juce::Rectangle<float> bounds,
                           float radius, bool pressed = false)
{
    const float travel = pressed ? 0.5f : 1.6f;

    g.setColour (juce::Colours::black.withAlpha (pressed ? 0.10f : 0.20f));
    g.fillRoundedRectangle (bounds.expanded (1.5f).translated (0.0f, travel + 1.4f),
                            radius + 1.5f);
    g.setColour (juce::Colours::black.withAlpha (pressed ? 0.12f : 0.20f));
    g.fillRoundedRectangle (bounds.translated (0.0f, travel * 0.5f), radius);

    g.setGradientFill (juce::ColourGradient (
        pressed ? porcelainPress : porcelainTop, bounds.getX(), bounds.getY(),
        pressed ? porcelainBottom.darker (0.04f) : porcelainBottom,
        bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, radius);

    g.setColour (juce::Colours::white.withAlpha (pressed ? 0.30f : 0.85f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), juce::jmax (0.5f, radius - 1.0f), 1.0f);
    g.setColour (porcelainEdge.withAlpha (pressed ? 0.80f : 0.62f));
    g.drawRoundedRectangle (bounds.reduced (0.25f), radius, 1.0f);
}

// A recessed capsule: the empty part of any value track.
inline void fillTrough (juce::Graphics& g, juce::Rectangle<float> bounds,
                        bool enabled = true)
{
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 1.0f), r);
    g.setColour (trough.withAlpha (enabled ? 1.0f : 0.45f));
    g.fillRoundedRectangle (bounds, r);
    g.setColour (juce::Colours::black.withAlpha (0.09f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), r, 1.0f);
}

// --- Sliders ------------------------------------------------------------------
//  A capsule trough, an accent fill grown from the zero end, and a porcelain
//  thumb INSET INSIDE the track. Because the thumb never exceeds the track the
//  whole control stays one clean capsule, which is what lets a row of them line
//  up the way the reference does.
inline void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, juce::Slider::SliderStyle style,
                              bool enabled, bool hover, bool down, bool focused,
                              juce::Colour accent = defaultAccent())
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y,
                                              (float) width, (float) height);
    const bool vertical = style == juce::Slider::LinearVertical
                       || style == juce::Slider::LinearBarVertical;
    const float alpha = enabled ? (hover ? 1.0f : 0.94f) : 0.34f;

    if (vertical)
    {
        // Wide enough to carry the thumb inside it. Capped so a tall thin slot
        // does not become a lozenge.
        const float w = juce::jlimit (14.0f, 30.0f, area.getWidth() * 0.62f);
        auto track = juce::Rectangle<float> (w, area.getHeight() - 2.0f)
                         .withCentre (area.getCentre());
        fillTrough (g, track, enabled);

        const float inset = juce::jmax (2.0f, w * 0.12f);
        auto lane = track.reduced (inset);
        const float valueY = juce::jlimit (lane.getY(), lane.getBottom(), sliderPos);
        auto value = lane.withTop (valueY);
        if (value.getHeight() > 0.5f)
        {
            setAccentGradient (g, lane, accent, alpha, true);
            g.fillRoundedRectangle (value, lane.getWidth() * 0.5f);
        }

        const float d = lane.getWidth();
        auto thumb = juce::Rectangle<float> (d, d)
                         .withCentre ({ lane.getCentreX(),
                                        juce::jlimit (lane.getY() + d * 0.5f,
                                                      lane.getBottom() - d * 0.5f, valueY) });
        fillPorcelain (g, thumb, d * 0.5f, down);
        if (focused) drawFocusRing (g, track.expanded (2.0f), track.getWidth() * 0.5f + 2.0f, accent);
        return;
    }

    const float h = juce::jlimit (12.0f, 26.0f, area.getHeight() * 0.62f);
    auto track = juce::Rectangle<float> (area.getWidth() - 2.0f, h)
                     .withCentre (area.getCentre());
    fillTrough (g, track, enabled);

    const float inset = juce::jmax (2.0f, h * 0.12f);
    auto lane = track.reduced (inset);
    const float valueX = juce::jlimit (lane.getX(), lane.getRight(), sliderPos);
    auto value = lane.withRight (valueX);
    if (value.getWidth() > 0.5f)
    {
        setAccentGradient (g, lane, accent, alpha);
        g.fillRoundedRectangle (value, lane.getHeight() * 0.5f);
    }

    const float d = lane.getHeight();
    auto thumb = juce::Rectangle<float> (d, d)
                     .withCentre ({ juce::jlimit (lane.getX() + d * 0.5f,
                                                  lane.getRight() - d * 0.5f, valueX),
                                    lane.getCentreY() });
    fillPorcelain (g, thumb, d * 0.5f, down);
    if (focused) drawFocusRing (g, track.expanded (2.0f), track.getHeight() * 0.5f + 2.0f, accent);
}

// --- Rotary -------------------------------------------------------------------
//  Same grammar bent into a circle: recessed arc, accent value arc, porcelain cap.
inline void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float startAngle, float endAngle,
                              bool enabled, bool hover, bool down, bool focused,
                              juce::Colour accent = defaultAccent())
{
    auto area = juce::Rectangle<float> ((float) x, (float) y,
                                        (float) width, (float) height).reduced (6.0f);
    const float radius = juce::jmax (8.0f, juce::jmin (area.getWidth(), area.getHeight()) * 0.5f);
    const auto centre = area.getCentre();
    const float arcRadius = radius - 3.0f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float lineWidth = juce::jlimit (3.0f, 6.5f, radius * 0.135f);
    const float alpha = enabled ? (hover ? 1.0f : 0.94f) : 0.32f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         startAngle, endAngle, true);
    g.setColour (trough.withAlpha (enabled ? 0.92f : 0.40f));
    g.strokePath (track, juce::PathStrokeType (
        lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (sliderPos > 0.0005f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             startAngle, angle, true);
        setAccentGradient (g, area, accent, alpha);
        g.strokePath (value, juce::PathStrokeType (
            lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    auto knob = juce::Rectangle<float> (radius * 1.44f, radius * 1.44f).withCentre (centre);
    fillPorcelain (g, knob, knob.getWidth() * 0.5f, down);

    // Pointer, not an engraved line: it has to read at 26px.
    const auto direction = juce::Point<float> (
        std::cos (angle - juce::MathConstants<float>::halfPi),
        std::sin (angle - juce::MathConstants<float>::halfPi));
    g.setColour (enabled ? ink.withAlpha (0.82f) : inkMuted.withAlpha (0.35f));
    g.drawLine ({ centre + direction * (radius * 0.30f),
                  centre + direction * (radius * 0.60f) },
                juce::jmax (1.8f, radius * 0.075f));

    if (focused)
    {
        g.setColour (accentLift (accent).withAlpha (0.95f));
        g.drawEllipse (knob.expanded (2.0f), 1.6f);
    }
}

// --- Buttons ------------------------------------------------------------------
//  Porcelain pill by default; when latched it fills with the accent and the text
//  layer flips to white (drawButtonText reads the toggle state to match).
inline void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                  bool hover, bool down,
                                  juce::Colour accent = defaultAccent())
{
    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const float r = bounds.getHeight() * 0.5f;
    const bool on = button.getToggleState();

    if (on)
    {
        g.setColour (juce::Colours::black.withAlpha (down ? 0.12f : 0.22f));
        g.fillRoundedRectangle (bounds.translated (0.0f, down ? 0.5f : 1.6f), r);
        setAccentGradient (g, bounds, accent, button.isEnabled() ? (hover ? 1.0f : 0.94f) : 0.34f);
        g.fillRoundedRectangle (bounds, r);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.drawRoundedRectangle (bounds.reduced (0.75f), r, 1.0f);
    }
    else
    {
        fillPorcelain (g, bounds, r, down);
        if (hover && button.isEnabled())
        {
            g.setColour (juce::Colours::white.withAlpha (0.30f));
            g.fillRoundedRectangle (bounds.reduced (1.5f), r - 1.5f);
        }
    }

    if (button.hasKeyboardFocus (true))
        drawFocusRing (g, bounds.expanded (1.5f), r + 1.5f, accent);
}

inline void drawComboBox (juce::Graphics& g, int width, int height, bool down,
                          bool focused, juce::Colour accent = defaultAccent())
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                      .reduced (1.0f);
    const float r = bounds.getHeight() * 0.5f;
    fillPorcelain (g, bounds, r, down);
    if (focused)
        drawFocusRing (g, bounds, r, accent);

    const float cx = (float) width - 17.0f;
    const float cy = (float) height * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (inkMuted);
    g.strokePath (arrow, juce::PathStrokeType (
        1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// --- Switch -------------------------------------------------------------------
//  Off: a recessed dark capsule with a porcelain knob at the left.
//  On:  the same capsule filled with the accent, knob at the right.
inline void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                              bool hover, bool down,
                              juce::Colour accent = defaultAccent())
{
    const auto bounds = button.getLocalBounds().toFloat();
    const float trackHeight = 22.0f;
    const float trackWidth  = 42.0f;
    auto track = juce::Rectangle<float> (
        bounds.getX(), bounds.getCentreY() - trackHeight * 0.5f, trackWidth, trackHeight);
    const float r = trackHeight * 0.5f;
    const bool on = button.getToggleState();
    const bool en = button.isEnabled();

    g.setColour (juce::Colours::black.withAlpha (0.24f));
    g.fillRoundedRectangle (track.translated (0.0f, 1.0f), r);

    if (on)
    {
        setAccentGradient (g, track, accent, en ? (hover ? 1.0f : 0.94f) : 0.34f);
        g.fillRoundedRectangle (track, r);
    }
    else
    {
        g.setColour (overlayHigh.withAlpha (en ? 1.0f : 0.5f));
        g.fillRoundedRectangle (track, r);
        g.setColour (juce::Colours::white.withAlpha (hover && en ? 0.14f : 0.07f));
        g.fillRoundedRectangle (track, r);
    }
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    g.drawRoundedRectangle (track.reduced (0.5f), r, 1.0f);

    const float pad = 2.5f;
    const float d = trackHeight - pad * 2.0f;
    const float knobX = on ? track.getRight() - d - pad : track.getX() + pad;
    fillPorcelain (g, { knobX, track.getY() + pad, d, d }, d * 0.5f, down);

    if (button.hasKeyboardFocus (true))
        drawFocusRing (g, track.expanded (2.0f), r + 2.0f, accent);

    if (button.getButtonText().isNotEmpty())
    {
        g.setColour (en ? overlayText.withAlpha (on ? 1.0f : 0.78f)
                        : overlayText.withAlpha (0.35f));
        g.setFont (sattari::brand::font (11.5f));
        g.drawText (button.getButtonText(),
                    bounds.withTrimmedLeft (trackWidth + 10.0f).toNearestInt(),
                    juce::Justification::centredLeft, true);
    }
}

inline void drawScrollbar (juce::Graphics& g, int x, int y, int width, int height,
                           bool vertical, int thumbStart, int thumbSize,
                           bool hover, bool down)
{
    auto lane = juce::Rectangle<float> ((float) x, (float) y,
                                        (float) width, (float) height).reduced (2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.fillRoundedRectangle (lane, juce::jmin (lane.getWidth(), lane.getHeight()) * 0.5f);

    juce::Rectangle<float> thumb;
    if (vertical)
        thumb = { (float) x + 2.0f, (float) thumbStart, (float) width - 4.0f, (float) thumbSize };
    else
        thumb = { (float) thumbStart, (float) y + 2.0f, (float) thumbSize, (float) height - 4.0f };

    g.setColour (juce::Colours::white.withAlpha (down ? 0.58f : (hover ? 0.44f : 0.26f)));
    g.fillRoundedRectangle (thumb, juce::jmin (thumb.getWidth(), thumb.getHeight()) * 0.5f);
}

inline void fillTextEditorBackground (juce::Graphics& g, int width, int height)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                      .reduced (0.5f);
    const float r = bounds.getHeight() * 0.5f;
    g.setColour (overlay);
    g.fillRoundedRectangle (bounds, r);
    g.setColour (juce::Colours::black.withAlpha (0.20f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), r, 1.0f);
}

inline void drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                   bool enabled, bool focused, bool readOnly,
                                   juce::Colour accent = defaultAccent())
{
    if (! enabled)
        return;

    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                      .reduced (0.5f);
    const float r = bounds.getHeight() * 0.5f;
    if (focused && ! readOnly)
        drawFocusRing (g, bounds, r, accent);
    else
    {
        g.setColour (overlayEdge.withAlpha (0.90f));
        g.drawRoundedRectangle (bounds, r, 1.0f);
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
                               juce::Font font, const juce::Path& tickShape,
                               juce::Colour accent = defaultAccent())
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
        // Accent, not the spectrum: a highlighted row is one state, so it is one hue.
        const float r = (float) row.getHeight() * 0.5f;
        setAccentGradient (g, row.toFloat(), accent, 0.92f);
        g.fillRoundedRectangle (row.toFloat(), r);
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
