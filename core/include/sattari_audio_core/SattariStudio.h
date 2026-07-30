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
#include <sattari_audio_core/SattariControlSurface.h>
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
    g.setFont (sattari::brand::fontOptions (10.5f, juce::Font::bold));
    g.drawText (title.toUpperCase(), row.removeFromLeft (juce::jmin (220, row.getWidth())),
                juce::Justification::centredLeft, false);
    if (detail.isNotEmpty())
    {
        g.setColour (colour::tertiary);
        g.setFont (sattari::brand::fontOptions (10.5f));
        g.drawText (detail, row, juce::Justification::centredRight, false);
    }
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit LookAndFeel (juce::uint32 accentArgb)
        : accent_ (accentArgb)
    {
        setDefaultSansSerifTypefaceName ("Helvetica");

        setColour (juce::ResizableWindow::backgroundColourId, colour::canvas);
        setColour (juce::Label::textColourId, colour::text);
        setColour (juce::Slider::rotarySliderFillColourId, accent_);
        setColour (juce::Slider::rotarySliderOutlineColourId, colour::separator);
        setColour (juce::Slider::thumbColourId, colour::text);
        setColour (juce::Slider::trackColourId, accent_);
        setColour (juce::Slider::backgroundColourId, colour::separator);
        setColour (juce::Slider::textBoxTextColourId, sattari::control::ink);
        setColour (juce::Slider::textBoxBackgroundColourId, sattari::control::porcelainTop);
        setColour (juce::Slider::textBoxOutlineColourId, sattari::control::porcelainEdge);
        setColour (juce::ComboBox::backgroundColourId, colour::surfaceHigh);
        setColour (juce::ComboBox::textColourId, sattari::control::ink);
        setColour (juce::ComboBox::outlineColourId, colour::separator);
        setColour (juce::ComboBox::arrowColourId, sattari::control::inkMuted);
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
        sattari::control::drawRotarySlider (
            g, x, y, width, height, sliderPos, startAngle, endAngle, slider.isEnabled());
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style,
                           juce::Slider& slider) override
    {
        juce::ignoreUnused (minSliderPos, maxSliderPos);
        if (style == juce::Slider::LinearHorizontal
            || style == juce::Slider::LinearVertical
            || style == juce::Slider::LinearBar
            || style == juce::Slider::LinearBarVertical)
        {
            sattari::control::drawLinearSlider (
                g, x, y, width, height, sliderPos, style, slider.isEnabled());
            return;
        }
        juce::LookAndFeel_V4::drawLinearSlider (
            g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool down,
                       int, int, int, int, juce::ComboBox& box) override
    {
        sattari::control::drawComboBox (
            g, width, height, down, box.hasKeyboardFocus (true));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setFont (sattari::brand::fontOptions (12.0f));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&, bool hover, bool down) override
    {
        sattari::control::drawButtonBackground (g, button, hover, down);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool, bool) override
    {
        g.setColour (button.isEnabled() ? sattari::control::ink
                                        : sattari::control::inkMuted.withAlpha (0.58f));
        g.setFont (sattari::brand::font (11.5f, juce::Font::bold));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (8, 2),
                          juce::Justification::centred, 1);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool hover, bool) override
    {
        sattari::control::drawToggleButton (g, button, hover);
    }

    juce::Font getLabelFont (juce::Label& label) override
    {
        const auto current = label.getFont();
        return sattari::brand::font (current.getHeight(), current.getStyleFlags());
    }

    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr
            && ! label.isBeingEdited())
        {
            auto bounds = label.getLocalBounds().toFloat().reduced (0.5f);
            sattari::control::fillPorcelain (
                g, bounds, juce::jmin (8.0f, bounds.getHeight() * 0.5f));
            g.setColour (sattari::control::ink);
            g.setFont (getLabelFont (label));
            g.drawFittedText (label.getText(), label.getLocalBounds().reduced (5, 1),
                              label.getJustificationType(), 1);
            return;
        }

        juce::LookAndFeel_V4::drawLabel (g, label);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return sattari::brand::font (
            juce::jlimit (10.0f, 15.0f, (float) buttonHeight * 0.42f), juce::Font::bold);
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return sattari::brand::font (
            juce::jlimit (10.0f, 14.0f, (float) box.getHeight() * 0.42f));
    }

    juce::Font getPopupMenuFont() override
    {
        return sattari::brand::font (13.0f);
    }

    juce::Font getSliderPopupFont (juce::Slider&) override
    {
        return sattari::brand::font (13.0f, juce::Font::bold);
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
        g.setFont (sattari::brand::fontOptions (23.0f, juce::Font::bold));
        g.drawText (sattari::branding::compactProductName (product_),
                    textArea.removeFromTop (34),
                    juce::Justification::bottomLeft, false);
        g.setColour (colour::secondary);
        g.setFont (sattari::brand::fontOptions (11.0f));
        g.drawText (category_, textArea.removeFromTop (20),
                    juce::Justification::topLeft, false);

        if (status_.isNotEmpty())
        {
            g.setFont (sattari::brand::fontOptions (10.0f, juce::Font::bold));
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
        g.setFont (sattari::brand::fontOptions (hero_ ? 11.5f : 10.0f, juce::Font::bold));
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
        g.setFont (sattari::brand::fontOptions (9.5f, juce::Font::bold));
        g.drawText (label_.toUpperCase(), area.removeFromTop (14),
                    juce::Justification::centredLeft, false);
        g.setColour (colour::text);
        g.setFont (sattari::brand::fontOptions (16.0f, juce::Font::bold));
        g.drawText (value_.isNotEmpty() ? value_ : "--", area.removeFromTop (22),
                    juce::Justification::centredLeft, false);
        if (detail_.isNotEmpty())
        {
            g.setColour (colour::secondary);
            g.setFont (sattari::brand::fontOptions (9.5f));
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
    label.setFont (sattari::brand::fontOptions (9.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, colour::tertiary);
    label.setJustificationType (justification);
    label.setAccessible (false);
}

} // namespace sattari::studio
