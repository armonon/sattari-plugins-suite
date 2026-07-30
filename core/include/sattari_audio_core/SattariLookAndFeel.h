#pragma once

// =============================================================================
//  Sattari Audio Suite - shared LookAndFeel (ultra-modern minimal)
// -----------------------------------------------------------------------------
//  One flat, restrained, near-monochrome look for every plugin editor: a flat
//  dark backdrop with a single hairline frame + a thin accent line, thin-ring
//  rotaries with a small indicator (no chunky knob), flat combos/buttons, and
//  switch-style toggles. One accent per product carries identity; everything
//  else is neutral and spacious.
//
//  All public method names are stable, so every editor inherits this look with
//  no per-editor changes. Construct one per editor with the product accent,
//  setLookAndFeel(&lnf) in the ctor, setLookAndFeel(nullptr) in the dtor.
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include "SattariBranding.h"
#include "SattariLogo.h"

namespace sattari::ui
{

class SattariLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit SattariLookAndFeel (juce::uint32 accentArgb = sattari::brand::kAccent)
        : accent (juce::Colour (accentArgb))
    {
        const auto text     = juce::Colour (sattari::brand::kTextPrimary);
        const auto muted     = juce::Colour (sattari::brand::kTextMuted);
        const auto panel     = juce::Colour (sattari::brand::kPanel);
        const auto raised    = juce::Colour (sattari::brand::kPanelRaised);
        const auto hairline   = juce::Colour (sattari::brand::kHairline);

        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (sattari::brand::kBackground));
        setColour (juce::Slider::rotarySliderFillColourId, accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, hairline);
        setColour (juce::Slider::thumbColourId, text);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, text);
        setColour (juce::ComboBox::backgroundColourId, raised);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::outlineColourId, hairline);
        setColour (juce::ComboBox::arrowColourId, muted);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.18f));
        setColour (juce::PopupMenu::highlightedTextColourId, text);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::TextButton::buttonColourId, raised);
        setColour (juce::TextButton::textColourOnId, text);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextEditor::backgroundColourId, raised);
        setColour (juce::TextEditor::outlineColourId, hairline);
    }

    juce::Colour getAccent() const noexcept { return accent; }

    // --- Shared backdrop: subtle blue gradient + hairline frame + accent top line ---
    void paintSuiteBackground (juce::Graphics& g, juce::Rectangle<int> bounds) const
    {
        // Soft vertical gradient from a deep ink-blue at the top to near-black at
        // the bottom; gives the suite the "premium-modern" feel without going loud.
        const auto top    = juce::Colour (sattari::brand::kBackground).brighter (0.04f);
        const auto bottom = juce::Colour (sattari::brand::kBackground).darker (0.10f);
        g.setGradientFill (juce::ColourGradient (top, bounds.toFloat().getX(), bounds.toFloat().getY(),
                                                  bottom, bounds.toFloat().getX(), bounds.toFloat().getBottom(),
                                                  false));
        g.fillRect (bounds);

        auto area = bounds.toFloat().reduced (sattari::brand::kPanelInset);
        g.setColour (juce::Colour (sattari::brand::kHairline));
        g.drawRoundedRectangle (area, sattari::brand::kCornerRadius, 1.0f);
        // a single thin accent line along the top edge = quiet product identity
        auto topLine = area.removeFromTop (2.0f).reduced (sattari::brand::kCornerRadius, 0.0f);
        g.setColour (accent.withAlpha (0.6f));
        g.fillRoundedRectangle (topLine, 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle  = startAngle + sliderPos * (endAngle - startAngle);
        const auto lineW  = juce::jmax (2.0f, radius * 0.07f);
        const auto arcR   = radius - lineW;

        // Recessed dial face: a soft dark disc inside the ring, like the Antares
        // knob look. Only drawn for "feature" knobs (radius > 28) to keep dense
        // multi-knob editors clean.
        const bool feature = radius > 28.0f;
        if (feature)
        {
            juce::ColourGradient dial (juce::Colour (sattari::brand::kPanelRaised), centre.x, centre.y - arcR * 0.4f,
                                       juce::Colour (sattari::brand::kPanelDeep),   centre.x, centre.y + arcR * 0.6f, false);
            g.setGradientFill (dial);
            g.fillEllipse (juce::Rectangle<float> (arcR * 1.5f, arcR * 1.5f).withCentre (centre));
            g.setColour (juce::Colour (sattari::brand::kHairline).withAlpha (0.7f));
            g.drawEllipse (juce::Rectangle<float> (arcR * 1.5f, arcR * 1.5f).withCentre (centre), 1.0f);
        }

        // Track (full arc, hairline).
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (sattari::brand::kHairline));
        g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Accent arc: feature knobs get a soft outer glow halo, then the bright
        // arc on top — the Antares "lit ring" look without going neon.
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        if (feature)
        {
            g.setColour (accent.withAlpha (0.18f));
            g.strokePath (arc, juce::PathStrokeType (lineW * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (accent);
        g.strokePath (arc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Indicator: a small accent dot at the value position. Feature knobs also
        // draw a short bright "pointer" line from the dial face to the dot.
        const juce::Point<float> tip (centre.x + arcR * std::cos (angle - juce::MathConstants<float>::halfPi),
                                      centre.y + arcR * std::sin (angle - juce::MathConstants<float>::halfPi));
        if (feature)
        {
            const juce::Point<float> innerTip (centre.x + (arcR * 0.55f) * std::cos (angle - juce::MathConstants<float>::halfPi),
                                               centre.y + (arcR * 0.55f) * std::sin (angle - juce::MathConstants<float>::halfPi));
            g.setColour (juce::Colour (sattari::brand::kTextPrimary).withAlpha (0.85f));
            g.drawLine ({ innerTip, tip }, lineW * 0.9f);
        }
        g.setColour (juce::Colour (sattari::brand::kTextPrimary));
        g.fillEllipse (juce::Rectangle<float> (lineW * 1.7f, lineW * 1.7f).withCentre (tip));
        juce::ignoreUnused (slider);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int, int, int, int, juce::ComboBox&) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
        g.setColour (juce::Colour (sattari::brand::kPanelRaised));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (isButtonDown ? accent.withAlpha (0.8f) : juce::Colour (sattari::brand::kHairline));
        g.drawRoundedRectangle (r, 7.0f, 1.0f);

        const float cx = (float) width - 16.0f, cy = (float) height * 0.5f;
        juce::Path chev;
        chev.startNewSubPath (cx - 4.0f, cy - 2.0f);
        chev.lineTo (cx, cy + 2.5f);
        chev.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (juce::Colour (sattari::brand::kTextMuted));
        g.strokePath (chev, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool shouldDrawHighlighted, bool shouldDrawDown) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto fill = juce::Colour (sattari::brand::kPanelRaised);
        if (shouldDrawDown)             fill = accent.withAlpha (0.20f);
        else if (shouldDrawHighlighted) fill = fill.brighter (0.12f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour ((shouldDrawDown || shouldDrawHighlighted) ? accent.withAlpha (0.85f)
                                                               : juce::Colour (sattari::brand::kHairline));
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
    }

    // Modern switch instead of JUCE's default checkbox tick.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                           bool shouldDrawHighlighted, bool shouldDrawDown) override
    {
        juce::ignoreUnused (shouldDrawHighlighted, shouldDrawDown);
        auto bounds = b.getLocalBounds().toFloat();
        const float trackW = 34.0f, trackH = 18.0f;
        juce::Rectangle<float> track (bounds.getX(), bounds.getCentreY() - trackH * 0.5f, trackW, trackH);
        const bool on = b.getToggleState();

        g.setColour (on ? accent.withAlpha (0.9f) : juce::Colour (sattari::brand::kPanelRaised));
        g.fillRoundedRectangle (track, trackH * 0.5f);
        if (! on)
        {
            g.setColour (juce::Colour (sattari::brand::kHairline));
            g.drawRoundedRectangle (track, trackH * 0.5f, 1.0f);
        }

        const float kd = trackH - 4.0f;
        const float kx = on ? track.getRight() - kd - 2.0f : track.getX() + 2.0f;
        g.setColour (juce::Colour (on ? sattari::brand::kBackground : sattari::brand::kTextMuted));
        g.fillEllipse (kx, track.getY() + 2.0f, kd, kd);

        auto textArea = bounds.withTrimmedLeft (trackW + 9.0f);
        g.setColour (juce::Colour (on ? sattari::brand::kTextPrimary : sattari::brand::kTextMuted));
        g.setFont (juce::FontOptions (sattari::brand::kLabelSize + 1.0f));
        g.drawText (b.getButtonText(), textArea.toNearestInt(), juce::Justification::centredLeft, true);
    }

    juce::Font getLabelFont (juce::Label& label) override
    {
        return label.getFont();
    }

    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        if (! (bool) label.getProperties().getWithDefault (brandTitleProperty, false)
            || label.isBeingEdited())
        {
            juce::LookAndFeel_V4::drawLabel (g, label);
            return;
        }

        auto area = label.getLocalBounds().reduced (1, 1);
        const int logoHeight = juce::jmin (26, juce::jmax (12, area.getHeight() - 2));
        const int logoWidth = juce::roundToInt ((float) logoHeight * 3.693f);
        auto logoArea = area.removeFromLeft (juce::jmin (logoWidth, area.getWidth()));
        sattari::branding::drawLogo (g, logoArea.toFloat());

        if (area.getWidth() <= 4)
            return;

        area.removeFromLeft (juce::jmin (9, area.getWidth()));
        g.setColour (label.findColour (juce::Label::textColourId));
        g.setFont (label.getFont());
        g.drawFittedText (label.getText(), area,
                          juce::Justification::centredLeft, 1, 0.78f);
    }

    // --- Convenience builders so editors stay short --------------------------
    static void styleRotary (juce::Slider& s, const juce::String& suffix = {})
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
        s.setTextValueSuffix (suffix);
    }

    static void styleTitle (juce::Label& l, const juce::String& text)
    {
        l.setTitle (text);
        l.setDescription (text + " audio plugin");
        l.getProperties().set (brandTitleProperty, true);
        l.setText (sattari::branding::compactProductName (text), juce::dontSendNotification);
        l.setFont (juce::FontOptions (sattari::brand::kTitleSize));   // regular weight = modern
        l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextPrimary));
    }

    static void styleSubtitle (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (sattari::brand::kSubtitleSize));
        l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    }

private:
    inline static const juce::Identifier brandTitleProperty { "sattariBrandTitle" };
    juce::Colour accent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariLookAndFeel)
};

} // namespace sattari::ui
