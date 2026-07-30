#pragma once

// =============================================================================
//  Sattari Audio Suite - shared LookAndFeel (ultra-modern minimal)
// -----------------------------------------------------------------------------
//  One restrained visual language for every plugin editor: a dark instrument
//  workspace with bright porcelain controls, spectrum value fills, sculpted
//  rotaries, pill buttons, and switch-style toggles. Helvetica keeps the dense
//  information layer calm while the controls provide tactile hierarchy.
//
//  All public method names are stable, so every editor inherits this look with
//  no per-editor changes. Construct one per editor with the product accent,
//  setLookAndFeel(&lnf) in the ctor, setLookAndFeel(nullptr) in the dtor.
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include "SattariBranding.h"
#include "SattariControlSurface.h"
#include "SattariLogo.h"

namespace sattari::ui
{

class SattariLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit SattariLookAndFeel (juce::uint32 accentArgb = sattari::brand::kAccent)
        : accent (juce::Colour (accentArgb))
    {
        setDefaultSansSerifTypefaceName ("Helvetica");

        const auto text     = juce::Colour (sattari::brand::kTextPrimary);
        const auto panel     = juce::Colour (sattari::brand::kPanel);
        const auto raised    = juce::Colour (sattari::brand::kPanelRaised);
        const auto hairline   = juce::Colour (sattari::brand::kHairline);

        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (sattari::brand::kBackground));
        setColour (juce::Slider::rotarySliderFillColourId, accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, hairline);
        setColour (juce::Slider::thumbColourId, text);
        setColour (juce::Slider::textBoxTextColourId, sattari::control::ink);
        setColour (juce::Slider::textBoxBackgroundColourId, sattari::control::porcelainTop);
        setColour (juce::Slider::textBoxOutlineColourId, sattari::control::porcelainEdge);
        setColour (juce::Label::textColourId, text);
        setColour (juce::ComboBox::backgroundColourId, raised);
        setColour (juce::ComboBox::textColourId, sattari::control::ink);
        setColour (juce::ComboBox::outlineColourId, hairline);
        setColour (juce::ComboBox::arrowColourId, sattari::control::inkMuted);
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

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int, int, int, int, juce::ComboBox& box) override
    {
        sattari::control::drawComboBox (
            g, width, height, isButtonDown, box.hasKeyboardFocus (true));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool shouldDrawHighlighted, bool shouldDrawDown) override
    {
        sattari::control::drawButtonBackground (
            g, b, shouldDrawHighlighted, shouldDrawDown);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool, bool) override
    {
        g.setColour (button.isEnabled() ? sattari::control::ink
                                        : sattari::control::inkMuted.withAlpha (0.58f));
        g.setFont (sattari::brand::font (11.5f, juce::Font::bold));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (10, 2),
                          juce::Justification::centred, 1);
    }

    // Modern switch instead of JUCE's default checkbox tick.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                           bool shouldDrawHighlighted, bool shouldDrawDown) override
    {
        juce::ignoreUnused (shouldDrawDown);
        sattari::control::drawToggleButton (g, b, shouldDrawHighlighted);
    }

    juce::Font getLabelFont (juce::Label& label) override
    {
        const auto current = label.getFont();
        return sattari::brand::font (current.getHeight(), current.getStyleFlags());
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
        l.setFont (sattari::brand::fontOptions (sattari::brand::kTitleSize));
        l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextPrimary));
    }

    static void styleSubtitle (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (sattari::brand::fontOptions (sattari::brand::kSubtitleSize));
        l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    }

private:
    inline static const juce::Identifier brandTitleProperty { "sattariBrandTitle" };
    juce::Colour accent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariLookAndFeel)
};

} // namespace sattari::ui
