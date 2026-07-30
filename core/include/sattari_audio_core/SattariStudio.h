#pragma once

// =============================================================================
//  Sattari Studio — shared product UI system
// -----------------------------------------------------------------------------
//  This is the suite's interface architecture, not another plugin skin:
//
//    * a quiet, high-contrast canvas and consistent elevated surfaces;
//    * one command header shared by every product;
//    * restrained product colour used for state and hierarchy, never decoration;
//    * reusable macro controls and live metric cards;
//    * keyboard/accessibility metadata configured with each component.
//
//  Editors own their product-specific workspace. Sattari Studio owns the visual
//  grammar, spacing and interaction behavior around it.
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLogo.h>

#include <cmath>

namespace sattari::studio
{

namespace colour
{
inline const juce::Colour canvas       { 0xff090a0d };
inline const juce::Colour canvasTop    { 0xff111319 };
inline const juce::Colour surface      { 0xff14161b };
inline const juce::Colour surfaceHigh  { 0xff1b1e25 };
inline const juce::Colour surfacePress { 0xff232731 };
inline const juce::Colour separator    { 0xff2a2e37 };
inline const juce::Colour text         { 0xfff4f5f7 };
inline const juce::Colour secondary    { 0xffa1a7b2 };
inline const juce::Colour tertiary     { 0xff686f7c };
inline const juce::Colour success      { 0xff68d7a7 };
inline const juce::Colour warning      { 0xffffc568 };
} // namespace colour

namespace metric
{
inline constexpr float outerMargin = 20.0f;
inline constexpr float gap = 12.0f;
inline constexpr float radius = 14.0f;
inline constexpr float controlRadius = 9.0f;
inline constexpr int headerHeight = 74;
inline constexpr int commandHeight = 38;
} // namespace metric

inline void paintCanvas (juce::Graphics& g, juce::Rectangle<int> bounds,
                         juce::Colour accent)
{
    const auto r = bounds.toFloat();
    g.setGradientFill (juce::ColourGradient (
        colour::canvasTop, r.getX(), r.getY(),
        colour::canvas, r.getX(), r.getY() + juce::jmin (260.0f, r.getHeight()),
        false));
    g.fillRect (r);

    // A single, extremely low-opacity product tint gives identity without
    // turning the interface into coloured glass.
    juce::ColourGradient halo (
        accent.withAlpha (0.075f), r.getX() + r.getWidth() * 0.18f, r.getY() - 30.0f,
        accent.withAlpha (0.0f), r.getX() + r.getWidth() * 0.18f, r.getY() + 240.0f,
        true);
    g.setGradientFill (halo);
    g.fillRect (r);
}

inline void paintSurface (juce::Graphics& g, juce::Rectangle<float> bounds,
                          bool elevated = false, float radius = metric::radius)
{
    if (elevated)
    {
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (bounds.translated (0.0f, 4.0f), radius);
    }

    g.setColour (elevated ? colour::surfaceHigh : colour::surface);
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (colour::separator.withAlpha (0.88f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
}

inline void paintSectionTitle (juce::Graphics& g, juce::Rectangle<int> bounds,
                               const juce::String& title,
                               const juce::String& detail = {})
{
    auto row = bounds;
    g.setColour (colour::secondary);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText (title.toUpperCase(), row.removeFromLeft (juce::jmin (220, row.getWidth())),
                juce::Justification::centredLeft, false);
    if (detail.isNotEmpty())
    {
        g.setColour (colour::tertiary);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (detail, row, juce::Justification::centredRight, false);
    }
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit LookAndFeel (juce::uint32 accentArgb)
        : accent_ (accentArgb)
    {
        setColour (juce::ResizableWindow::backgroundColourId, colour::canvas);
        setColour (juce::Label::textColourId, colour::text);
        setColour (juce::Slider::rotarySliderFillColourId, accent_);
        setColour (juce::Slider::rotarySliderOutlineColourId, colour::separator);
        setColour (juce::Slider::thumbColourId, colour::text);
        setColour (juce::Slider::trackColourId, accent_);
        setColour (juce::Slider::backgroundColourId, colour::separator);
        setColour (juce::Slider::textBoxTextColourId, colour::secondary);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::backgroundColourId, colour::surfaceHigh);
        setColour (juce::ComboBox::textColourId, colour::text);
        setColour (juce::ComboBox::outlineColourId, colour::separator);
        setColour (juce::ComboBox::arrowColourId, colour::secondary);
        setColour (juce::PopupMenu::backgroundColourId, colour::surfaceHigh);
        setColour (juce::PopupMenu::textColourId, colour::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent_.withAlpha (0.20f));
        setColour (juce::PopupMenu::highlightedTextColourId, colour::text);
        setColour (juce::ToggleButton::textColourId, colour::secondary);
        setColour (juce::TextButton::buttonColourId, colour::surfaceHigh);
        setColour (juce::TextButton::textColourOnId, colour::text);
        setColour (juce::TextButton::textColourOffId, colour::secondary);
        setColour (juce::TextEditor::backgroundColourId, colour::surfaceHigh);
        setColour (juce::TextEditor::textColourId, colour::text);
        setColour (juce::TextEditor::outlineColourId, colour::separator);
    }

    juce::Colour accent() const noexcept { return accent_; }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (8.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float arcRadius = radius - 4.0f;
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        const float line = juce::jlimit (2.0f, 4.0f, radius * 0.075f);

        g.setColour (colour::surfaceHigh);
        g.fillEllipse (juce::Rectangle<float> (radius * 1.56f, radius * 1.56f)
                           .withCentre (centre));

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             startAngle, endAngle, true);
        g.setColour (colour::separator);
        g.strokePath (track, juce::PathStrokeType (
            line, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             startAngle, angle, true);
        g.setColour (slider.isEnabled() ? accent_ : colour::tertiary);
        g.strokePath (value, juce::PathStrokeType (
            line, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto direction = juce::Point<float> (
            std::cos (angle - juce::MathConstants<float>::halfPi),
            std::sin (angle - juce::MathConstants<float>::halfPi));
        g.setColour (slider.isEnabled() ? colour::text : colour::tertiary);
        g.drawLine ({ centre + direction * (arcRadius * 0.43f),
                      centre + direction * (arcRadius * 0.72f) },
                    juce::jmax (1.5f, line * 0.65f));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style,
                           juce::Slider& slider) override
    {
        juce::ignoreUnused (minSliderPos, maxSliderPos);
        if (style == juce::Slider::LinearHorizontal
            || style == juce::Slider::LinearBar
            || style == juce::Slider::LinearBarVertical)
        {
            const float cy = (float) y + (float) height * 0.5f;
            auto track = juce::Rectangle<float> ((float) x, cy - 2.0f,
                                                  (float) width, 4.0f).reduced (3.0f, 0.0f);
            g.setColour (colour::separator);
            g.fillRoundedRectangle (track, 2.0f);
            g.setColour (slider.isEnabled() ? accent_ : colour::tertiary);
            g.fillRoundedRectangle (track.withRight (
                juce::jlimit (track.getX(), track.getRight(), sliderPos)), 2.0f);
            g.setColour (slider.isEnabled() ? colour::text : colour::tertiary);
            g.fillEllipse (sliderPos - 5.0f, cy - 5.0f, 10.0f, 10.0f);
            return;
        }
        juce::LookAndFeel_V4::drawLinearSlider (
            g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool down,
                       int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                     .reduced (0.5f);
        g.setColour (down ? colour::surfacePress : colour::surfaceHigh);
        g.fillRoundedRectangle (r, metric::controlRadius);
        g.setColour (box.hasKeyboardFocus (true) ? accent_.withAlpha (0.85f)
                                                  : colour::separator);
        g.drawRoundedRectangle (r, metric::controlRadius, 1.0f);

        const float cx = (float) width - 15.0f;
        const float cy = (float) height * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath (cx - 3.5f, cy - 1.5f);
        arrow.lineTo (cx, cy + 2.0f);
        arrow.lineTo (cx + 3.5f, cy - 1.5f);
        g.setColour (colour::secondary);
        g.strokePath (arrow, juce::PathStrokeType (
            1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setFont (juce::FontOptions (12.0f));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&, bool hover, bool down) override
    {
        auto r = button.getLocalBounds().toFloat().reduced (0.5f);
        auto fill = button.getToggleState() ? accent_.withAlpha (0.16f)
                  : down ? colour::surfacePress
                         : hover ? colour::surfaceHigh.brighter (0.045f)
                                 : colour::surfaceHigh;
        if (! button.isEnabled()) fill = colour::surface;
        g.setColour (fill);
        g.fillRoundedRectangle (r, metric::controlRadius);
        g.setColour ((button.getToggleState() || button.hasKeyboardFocus (true))
                         ? accent_.withAlpha (0.85f)
                         : colour::separator);
        g.drawRoundedRectangle (r, metric::controlRadius, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool, bool) override
    {
        g.setColour (button.isEnabled() ? colour::text : colour::tertiary);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (8, 2),
                          juce::Justification::centred, 1);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool hover, bool) override
    {
        const auto bounds = button.getLocalBounds().toFloat();
        const bool on = button.getToggleState();
        const float h = 20.0f;
        juce::Rectangle<float> track (
            bounds.getX(), bounds.getCentreY() - h * 0.5f, 36.0f, h);
        g.setColour (on ? accent_ : (hover ? colour::surfacePress : colour::surfaceHigh));
        g.fillRoundedRectangle (track, h * 0.5f);
        if (! on)
        {
            g.setColour (colour::separator);
            g.drawRoundedRectangle (track, h * 0.5f, 1.0f);
        }

        const float diameter = 16.0f;
        const float knobX = on ? track.getRight() - diameter - 2.0f : track.getX() + 2.0f;
        g.setColour (on ? colour::canvas : colour::secondary);
        g.fillEllipse (knobX, track.getY() + 2.0f, diameter, diameter);

        g.setColour (button.isEnabled() ? (on ? colour::text : colour::secondary)
                                         : colour::tertiary);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (button.getButtonText(),
                    bounds.withTrimmedLeft (46.0f).toNearestInt(),
                    juce::Justification::centredLeft, true);
    }

private:
    juce::Colour accent_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LookAndFeel)
};

class Header final : public juce::Component
{
public:
    Header (juce::String productName, juce::String category, juce::Colour accent)
        : product_ (std::move (productName)),
          category_ (std::move (category)),
          accent_ (accent)
    {
        setAccessible (true);
        setTitle (product_);
        setDescription (category_);
    }

    void setStatus (juce::String status, bool active)
    {
        if (status_ == status && active_ == active) return;
        status_ = std::move (status);
        active_ = active;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        auto logoArea = area.removeFromLeft (136).withSizeKeepingCentre (126, 42);
        sattari::branding::drawLogo (g, logoArea.toFloat());
        area.removeFromLeft (12);
        const int statusWidth = status_.isNotEmpty()
            ? juce::jlimit (96, 184, 30 + status_.length() * 7)
            : 0;
        const int textWidth = juce::jmax (
            180, juce::jmin (
                470, area.getWidth()
                    - (statusWidth > 0 ? statusWidth + 12 : 0)));
        auto textArea = area.removeFromLeft (
            juce::jmin (textWidth, area.getWidth()));
        g.setColour (colour::text);
        g.setFont (juce::FontOptions (23.0f, juce::Font::bold));
        g.drawText (sattari::branding::compactProductName (product_),
                    textArea.removeFromTop (34),
                    juce::Justification::bottomLeft, false);
        g.setColour (colour::secondary);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (category_, textArea.removeFromTop (20),
                    juce::Justification::topLeft, false);

        if (status_.isNotEmpty())
        {
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            auto pill = area.removeFromRight (statusWidth)
                            .withSizeKeepingCentre (statusWidth, 26).toFloat();
            g.setColour ((active_ ? accent_ : colour::surfaceHigh)
                             .withAlpha (active_ ? 0.17f : 1.0f));
            g.fillRoundedRectangle (pill, 13.0f);
            g.setColour (active_ ? accent_ : colour::separator);
            g.drawRoundedRectangle (pill.reduced (0.5f), 13.0f, 1.0f);
            g.setColour (active_ ? accent_ : colour::secondary);
            g.fillEllipse (pill.getX() + 10.0f, pill.getCentreY() - 3.0f, 6.0f, 6.0f);
            g.drawText (status_, pill.withTrimmedLeft (21.0f).toNearestInt(),
                        juce::Justification::centred, false);
        }
    }

private:
    juce::String product_, category_, status_;
    juce::Colour accent_;
    bool active_ = false;
};

class MacroControl final : public juce::Component
{
public:
    MacroControl (juce::String name, juce::String suffix = {}, bool hero = false)
        : name_ (std::move (name)), hero_ (hero)
    {
        slider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider_.setTextBoxStyle (juce::Slider::TextBoxBelow, false,
                                 hero ? 92 : 76, hero ? 24 : 20);
        slider_.setTextValueSuffix (suffix);
        slider_.setTitle (name_);
        slider_.setDescription ("Adjust " + name_);
        slider_.setAccessible (true);
        addAndMakeVisible (slider_);
        setTitle (name_);
    }

    juce::Slider& slider() noexcept { return slider_; }
    const juce::Slider& slider() const noexcept { return slider_; }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colour::secondary);
        g.setFont (juce::FontOptions (hero_ ? 11.5f : 10.0f, juce::Font::bold));
        g.drawText (name_.toUpperCase(), getLocalBounds().removeFromTop (18),
                    juce::Justification::centred, false);
    }

    void resized() override
    {
        slider_.setBounds (getLocalBounds().withTrimmedTop (18));
    }

private:
    juce::String name_;
    bool hero_ = false;
    juce::Slider slider_;
};

class MetricCard final : public juce::Component
{
public:
    explicit MetricCard (juce::String label) : label_ (std::move (label))
    {
        setAccessible (true);
        setTitle (label_);
    }

    void setValue (juce::String value, juce::String detail = {})
    {
        if (value_ == value && detail_ == detail) return;
        value_ = std::move (value);
        detail_ = std::move (detail);
        setDescription (label_ + ": " + value_ + " " + detail_);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        paintSurface (g, getLocalBounds().toFloat(), false, 11.0f);
        auto area = getLocalBounds().reduced (12, 8);
        g.setColour (colour::tertiary);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (label_.toUpperCase(), area.removeFromTop (14),
                    juce::Justification::centredLeft, false);
        g.setColour (colour::text);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (value_.isNotEmpty() ? value_ : "--", area.removeFromTop (22),
                    juce::Justification::centredLeft, false);
        if (detail_.isNotEmpty())
        {
            g.setColour (colour::secondary);
            g.setFont (juce::FontOptions (9.5f));
            g.drawText (detail_, area, juce::Justification::centredLeft, false);
        }
    }

private:
    juce::String label_, value_, detail_;
};

inline void styleFieldLabel (juce::Label& label, const juce::String& text,
                             juce::Justification justification = juce::Justification::centredLeft)
{
    label.setText (text.toUpperCase(), juce::dontSendNotification);
    label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, colour::tertiary);
    label.setJustificationType (justification);
    label.setAccessible (false);
}

} // namespace sattari::studio
