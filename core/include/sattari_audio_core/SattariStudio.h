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
inline const juce::Colour canvas       { 0xff1c1d21 };
inline const juce::Colour canvasTop    { 0xff26272d };
inline const juce::Colour surface      { 0xff26272c };
inline const juce::Colour surfaceHigh  { 0xff2e3036 };
inline const juce::Colour surfacePress { 0xff383a42 };
inline const juce::Colour separator    { 0xff3a3d45 };
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
inline constexpr float radius = 16.0f;
inline constexpr float controlRadius = 9.0f;
inline constexpr int headerHeight = 78;
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

    // Quiet centre illumination keeps large editors dimensional while leaving
    // the perimeter visually calm around the host window.
    juce::ColourGradient illumination (
        juce::Colours::white.withAlpha (0.018f), r.getCentreX(), r.getY() + r.getHeight() * 0.36f,
        juce::Colours::transparentWhite, r.getCentreX(), r.getBottom(), true);
    g.setGradientFill (illumination);
    g.fillRect (r);
}

inline void paintSurface (juce::Graphics& g, juce::Rectangle<float> bounds,
                          bool elevated = false, float radius = metric::radius)
{
    if (elevated)
    {
        g.setColour (juce::Colours::black.withAlpha (0.10f));
        g.fillRoundedRectangle (bounds.expanded (4.0f).translated (0.0f, 6.0f), radius + 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRoundedRectangle (bounds.expanded (1.0f).translated (0.0f, 3.0f), radius + 1.0f);
    }

    g.setGradientFill (juce::ColourGradient (
        elevated ? colour::surfaceHigh.brighter (0.025f) : colour::surface.brighter (0.018f),
        bounds.getX(), bounds.getY(),
        elevated ? colour::surfaceHigh.darker (0.035f) : colour::surface.darker (0.025f),
        bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (colour::separator.withAlpha (0.88f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (elevated ? 0.055f : 0.032f));
    g.drawRoundedRectangle (bounds.reduced (1.5f), juce::jmax (1.0f, radius - 1.0f), 1.0f);
}

// Uppercase without tracking reads as a jammed block — the letterforms have no
// descenders or x-height variation to separate them. JUCE has no tracking, so we
// advance per glyph. Only ever used on short metadata strings.
inline float trackedTextWidth (const juce::Font& font, const juce::String& text,
                               float tracking)
{
    if (text.isEmpty()) return 0.0f;
    return juce::GlyphArrangement::getStringWidth (font, text)
         + tracking * (float) (text.length() - 1);
}

inline void drawTrackedText (juce::Graphics& g, const juce::String& text,
                             juce::Rectangle<int> bounds, const juce::Font& font,
                             float tracking, juce::Justification just)
{
    if (text.isEmpty()) return;
    g.setFont (font);
    const float total = trackedTextWidth (font, text, tracking);
    float x = (float) bounds.getX();
    if (just.testFlags (juce::Justification::right))
        x = (float) bounds.getRight() - total;
    else if (just.testFlags (juce::Justification::horizontallyCentred))
        x = bounds.toFloat().getCentreX() - total * 0.5f;

    const auto row = bounds.toFloat();
    for (int i = 0; i < text.length(); ++i)
    {
        const auto ch = text.substring (i, i + 1);
        const float w = juce::GlyphArrangement::getStringWidth (font, ch);
        g.drawText (ch, juce::Rectangle<float> (x, row.getY(), w + 2.0f, row.getHeight()),
                    juce::Justification::centredLeft, false);
        x += w + tracking;
    }
}

inline void paintSectionTitle (juce::Graphics& g, juce::Rectangle<int> bounds,
                               const juce::String& title,
                               const juce::String& detail = {})
{
    auto row = bounds;
    g.setColour (colour::secondary);
    const auto titleFont = juce::Font (sattari::brand::fontOptions (10.0f, juce::Font::bold));
    const auto upper = title.toUpperCase();
    // Take only the width the title actually needs. A fixed reservation steals
    // room from the right-aligned detail and silently truncates it.
    const int titleW = juce::jmin (row.getWidth(),
                                   juce::roundToInt (trackedTextWidth (titleFont, upper, 0.9f)) + 4);
    drawTrackedText (g, upper, row.removeFromLeft (titleW),
                     titleFont, 0.9f, juce::Justification::left);
    row.removeFromLeft (juce::jmin (12, row.getWidth()));
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
            g, x, y, width, height, sliderPos, startAngle, endAngle,
            slider.isEnabled(), slider.isMouseOverOrDragging(),
            slider.isMouseButtonDown(), slider.hasKeyboardFocus (true), accent_);
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
                g, x, y, width, height, sliderPos, style,
                slider.isEnabled(), slider.isMouseOverOrDragging(),
                slider.isMouseButtonDown(), slider.hasKeyboardFocus (true), accent_);
            return;
        }
        juce::LookAndFeel_V4::drawLinearSlider (
            g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool down,
                       int, int, int, int, juce::ComboBox& box) override
    {
        sattari::control::drawComboBox (
            g, width, height, down, box.hasKeyboardFocus (true), accent_);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setFont (sattari::brand::fontOptions (12.0f));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&, bool hover, bool down) override
    {
        sattari::control::drawButtonBackground (g, button, hover, down, accent_);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool, bool) override
    {
        // A latched button is accent-filled, so its label has to invert with it.
        const auto on = button.getToggleState();
        g.setColour (! button.isEnabled()
                         ? sattari::control::inkMuted.withAlpha (0.58f)
                         : (on ? juce::Colours::white : sattari::control::ink));
        g.setFont (sattari::brand::font (11.5f, juce::Font::bold));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (8, 2),
                          juce::Justification::centred, 1);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool hover, bool down) override
    {
        sattari::control::drawToggleButton (g, button, hover, down, accent_);
    }

    int getDefaultScrollbarWidth() override { return 8; }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y,
                        int width, int height, bool vertical, int thumbStart,
                        int thumbSize, bool hover, bool down) override
    {
        sattari::control::drawScrollbar (
            g, x, y, width, height, vertical, thumbStart, thumbSize, hover, down);
    }

    void fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                   juce::TextEditor&) override
    {
        sattari::control::fillTextEditorBackground (g, width, height);
    }

    void drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                juce::TextEditor& editor) override
    {
        sattari::control::drawTextEditorOutline (
            g, width, height, editor.isEnabled(),
            editor.hasKeyboardFocus (true), editor.isReadOnly(), accent_);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        sattari::control::drawPopupMenuBackground (g, width, height);
    }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                            bool separator, bool active, bool highlighted,
                            bool ticked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcut, const juce::Drawable* icon,
                            const juce::Colour* textColour) override
    {
        sattari::control::drawPopupMenuItem (
            g, area, separator, active, highlighted, ticked, hasSubMenu,
            text, shortcut, icon, textColour, getPopupMenuFont(), getTickShape (1.0f), accent_);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text,
                      int width, int height) override
    {
        sattari::control::drawTooltip (g, text, width, height);
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

    //  Hierarchy, top to bottom: brand, then what KIND of product this is, then
    //  which one. The category rides ABOVE the name as an accent eyebrow rather
    //  than below it as grey subtitle, which fixes two things at once — the
    //  accent finally appears in the header, where product identity belongs, and
    //  a short name stops looking stranded. "Arp" under a wide eyebrow reads as
    //  deliberate; "Arp" over a wider grey subtitle just looked unbalanced.
    //
    //  The lockup gets its own zone behind a hairline instead of sitting inline.
    //  It is a 3.69:1 image with a tagline line in it, so drawing it 42px tall
    //  rendered that tagline as illegible mush AND made it compete with the
    //  product name for the same job. Smaller and fenced off, it reads as a mark.
    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();

        constexpr int kLogoZone = 112;
        constexpr int kLogoW    = 96;
        auto logoArea = area.removeFromLeft (kLogoZone)
                            .withSizeKeepingCentre (kLogoW,
                                juce::roundToInt ((float) kLogoW / 3.693f));
        sattari::branding::drawLogo (g, logoArea.toFloat());

        g.setColour (colour::separator);
        g.fillRect (juce::Rectangle<int> (area.getX(), area.getCentreY() - 15, 1, 30));
        area.removeFromLeft (17);

        // Status pill, measured from its own text instead of a per-character guess.
        if (status_.isNotEmpty())
        {
            const auto statusFont = juce::Font (sattari::brand::fontOptions (9.5f, juce::Font::bold));
            const int textW = juce::roundToInt (trackedTextWidth (statusFont, status_.toUpperCase(), 1.0f));
            const int pillW = juce::jmin (area.getWidth(), textW + 40);
            auto pill = area.removeFromRight (pillW)
                            .withSizeKeepingCentre (pillW, 26).toFloat();
            g.setColour (active_ ? accent_.withAlpha (0.16f) : colour::surfaceHigh);
            g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
            g.setColour (active_ ? accent_.withAlpha (0.55f) : colour::separator);
            g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);
            g.setColour (active_ ? accent_ : colour::tertiary);
            g.fillEllipse (pill.getX() + 13.0f, pill.getCentreY() - 3.0f, 6.0f, 6.0f);
            g.setColour (active_ ? accent_ : colour::secondary);
            drawTrackedText (g, status_.toUpperCase(),
                             pill.withTrimmedLeft (25.0f).withTrimmedRight (6.0f).toNearestInt(),
                             statusFont, 1.0f, juce::Justification::left);
            area.removeFromRight (14);
        }

        // Accent tick: the product's colour, at the point the eye enters the name.
        auto tick = juce::Rectangle<float> ((float) area.getX(),
                                            (float) area.getCentreY() - 17.0f, 3.0f, 34.0f);
        g.setColour (accent_);
        g.fillRoundedRectangle (tick, 1.5f);
        area.removeFromLeft (15);

        const juce::String name = sattari::branding::compactProductName (product_);
        auto block = area.withSizeKeepingCentre (area.getWidth(), 44);

        if (category_.isNotEmpty())
        {
            g.setColour (accent_);
            drawTrackedText (g, category_.toUpperCase(), block.removeFromTop (13),
                             juce::Font (sattari::brand::fontOptions (9.5f, juce::Font::bold)),
                             1.15f, juce::Justification::left);
            block.removeFromTop (2);
        }

        g.setColour (colour::text);
        g.setFont (sattari::brand::fontOptions (27.0f, juce::Font::bold));
        g.drawText (name, block, juce::Justification::topLeft, false);
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
