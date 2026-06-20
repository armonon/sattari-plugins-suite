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

    // --- Shared backdrop: flat dark + hairline frame + thin accent top line ---
    void paintSuiteBackground (juce::Graphics& g, juce::Rectangle<int> bounds) const
    {
        g.fillAll (juce::Colour (sattari::brand::kBackground));
        auto area = bounds.toFloat().reduced (sattari::brand::kPanelInset);
        g.setColour (juce::Colour (sattari::brand::kHairline));
        g.drawRoundedRectangle (area, sattari::brand::kCornerRadius, 1.0f);
        // a single thin accent line along the top edge = quiet product identity
        auto top = area.removeFromTop (2.0f).reduced (sattari::brand::kCornerRadius, 0.0f);
        g.setColour (accent.withAlpha (0.6f));
        g.fillRoundedRectangle (top, 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle  = startAngle + sliderPos * (endAngle - startAngle);
        const auto lineW  = juce::jmax (2.0f, radius * 0.07f);   // thin ring
        const auto arcR   = radius - lineW;

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (sattari::brand::kHairline));
        g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (accent);
        g.strokePath (arc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // small indicator dot at the value position; clean empty centre.
        const juce::Point<float> tip (centre.x + arcR * std::cos (angle - juce::MathConstants<float>::halfPi),
                                      centre.y + arcR * std::sin (angle - juce::MathConstants<float>::halfPi));
        g.setColour (juce::Colour (sattari::brand::kTextPrimary));
        g.fillEllipse (juce::Rectangle<float> (lineW * 1.7f, lineW * 1.7f).withCentre (tip));
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

    // --- Convenience builders so editors stay short --------------------------
    static void styleRotary (juce::Slider& s, const juce::String& suffix = {})
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
        s.setTextValueSuffix (suffix);
    }

    static void styleTitle (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
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
    juce::Colour accent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariLookAndFeel)
};

} // namespace sattari::ui
