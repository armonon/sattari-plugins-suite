#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLogo.h>
#include <array>
#include <cmath>

namespace sattari::releaseui
{
inline const juce::Colour canvas { 0xff141618 }, surface { 0xff1c1f22 }, raised { 0xff262a2e };
inline const juce::Colour line { 0xff363c41 }, text { 0xffedf1f2 }, muted { 0xffa5afb5 };
inline const juce::Colour faint { 0xff748087 }, warning { 0xffedb86c }, red { 0xffed857c };

enum class Icon { save, undo, up, down, swap, power };

inline void icon (juce::Button& button, Icon value, const juce::String& title)
{
    button.getProperties().set ("releaseIcon", (int) value);
    button.setTitle (title);
    button.setTooltip (title);
    button.setDescription (title);
}

inline void primary (juce::Button& button) { button.getProperties().set ("releasePrimary", true); }

class IconButton final : public juce::TextButton
{
public:
    IconButton (Icon value, const juce::String& title) { icon (*this, value, title); }
};

inline void caption (juce::Label& label, const juce::String& value)
{
    label.setText (value, juce::dontSendNotification);
    label.setFont (sattari::brand::fontOptions (11.0f));
    label.setColour (juce::Label::textColourId, muted);
}

inline void section (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title,
                     const juce::String& detail = {})
{
    g.setFont (sattari::brand::fontOptions (12.0f, juce::Font::bold));
    g.setColour (text);
    g.drawText (title, r.removeFromLeft (juce::jmin (180, r.getWidth() / 2)), juce::Justification::centredLeft);
    g.setColour (muted);
    g.setFont (sattari::brand::fontOptions (11.0f));
    g.drawText (detail, r, juce::Justification::centredRight);
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit LookAndFeel (juce::uint32 colour) : accent (colour)
    {
#if JUCE_WINDOWS
        setDefaultSansSerifTypefaceName ("Segoe UI");
#else
        setDefaultSansSerifTypefaceName ("Helvetica");
#endif
        setColour (juce::ResizableWindow::backgroundColourId, canvas);
        setColour (juce::Label::textColourId, text);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxBackgroundColourId, canvas);
        setColour (juce::Slider::textBoxOutlineColourId, line);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::backgroundColourId, surface);
        setColour (juce::ComboBox::outlineColourId, line);
        setColour (juce::PopupMenu::backgroundColourId, surface);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, raised);
        setColour (juce::PopupMenu::highlightedTextColourId, accent);
        setColour (juce::TextEditor::backgroundColourId, canvas);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::outlineColourId, line);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.3f));
        setColour (juce::CaretComponent::caretColourId, accent);
        setColour (juce::TooltipWindow::backgroundColourId, raised);
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, line);
        setColour (juce::ScrollBar::thumbColourId, line.brighter (0.15f));

        // Lucide SVG geometry, ISC/MIT licensed; notices are in assets/icons/LICENSE.lucide.
        const char* paths[] = {
            R"(<path d="M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/><path d="M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7M7 3v4a1 1 0 0 0 1 1h7"/>)",
            R"(<path d="M9 14 4 9l5-5M4 9h10.5a5.5 5.5 0 0 1 5.5 5.5a5.5 5.5 0 0 1-5.5 5.5H11"/>)",
            R"(<path d="m5 12 7-7 7 7M12 19V5"/>)",
            R"(<path d="M12 5v14m7-7-7 7-7-7"/>)",
            R"(<path d="m16 3 4 4-4 4M20 7H4m4 14-4-4 4-4M4 17h16"/>)",
            R"(<path d="M12 2v10M18.4 6.6a9 9 0 1 1-12.77.04"/>)"
        };
        for (size_t i = 0; i < icons.size(); ++i)
            if (auto xml = juce::parseXML (juce::String (R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#edf1f2" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">)") + paths[i] + "</svg>"))
                icons[i] = juce::Drawable::createFromSVG (*xml);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (sattari::brand::fontOptions (12.0f, juce::Font::bold)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (sattari::brand::fontOptions (12.0f)); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const bool strong = (bool) b.getProperties().getWithDefault ("releasePrimary", false);
        const bool on = (bool) b.getProperties().getWithDefault ("releaseInvertToggle", false) ? ! b.getToggleState() : b.getToggleState();
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto fill = strong ? accent : on ? accent.withAlpha (0.17f) : over ? raised.brighter (0.1f) : raised;
        if (down) fill = fill.darker (0.15f);
        g.setColour (fill.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.35f));
        g.fillRoundedRectangle (r, 4);
        g.setColour ((on || b.hasKeyboardFocus (true) ? accent : line).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.drawRoundedRectangle (r, 4, 1);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const float opacity = b.isEnabled() ? 1.0f : 0.38f;
        if (b.getProperties().contains ("releaseIcon"))
        {
            const auto i = (size_t) (int) b.getProperties()["releaseIcon"];
            if (i < icons.size() && icons[i])
                icons[i]->drawWithin (g, b.getLocalBounds().toFloat().withSizeKeepingCentre (17, 17), juce::RectanglePlacement::centred, opacity);
            return;
        }
        g.setColour (((bool) b.getProperties().getWithDefault ("releasePrimary", false) ? canvas : b.getToggleState() ? accent : text).withAlpha (opacity));
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (7, 0), juce::Justification::centred, 1, 0.85f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        const auto alpha = b.isEnabled() ? 1.0f : 0.38f;
        auto box = juce::Rectangle<float> (1, (float) b.getHeight() / 2 - 8, 16, 16);
        g.setColour ((b.getToggleState() ? accent : over ? raised.brighter (0.2f) : raised).withAlpha (alpha));
        g.fillRoundedRectangle (box, 3);
        g.setColour ((b.hasKeyboardFocus (true) ? accent : line).withAlpha (alpha));
        g.drawRoundedRectangle (box, 3, 1);
        if (b.getToggleState())
        {
            juce::Path tick; tick.startNewSubPath (4, box.getCentreY()); tick.lineTo (7, box.getCentreY() + 3); tick.lineTo (13, box.getCentreY() - 4);
            g.setColour (canvas.withAlpha (alpha)); g.strokePath (tick, juce::PathStrokeType (1.8f));
        }
        g.setColour (text.withAlpha (alpha)); g.setFont (sattari::brand::fontOptions (12.0f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (25), juce::Justification::centredLeft, 1);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& b) override
    {
        const auto r = juce::Rectangle<float> (0.5f, 0.5f, (float) w - 1, (float) h - 1);
        g.setColour (down ? raised : surface); g.fillRoundedRectangle (r, 4);
        g.setColour (b.hasKeyboardFocus (true) ? accent : line); g.drawRoundedRectangle (r, 4, 1);
        juce::Path p; p.startNewSubPath ((float) w - 19, (float) h / 2 - 2); p.lineTo ((float) w - 15, (float) h / 2 + 2); p.lineTo ((float) w - 11, (float) h / 2 - 2);
        g.setColour (muted.withAlpha (b.isEnabled() ? 1.0f : 0.35f)); g.strokePath (p, juce::PathStrokeType (1.4f));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    { label.setBounds (10, 1, juce::jmax (1, box.getWidth() - 36), box.getHeight() - 2); label.setFont (getComboBoxFont (box)); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float value, float start, float end, juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (8);
        const float diameter = juce::jmin (r.getWidth(), r.getHeight(), 96.0f);
        r = r.withSizeKeepingCentre (diameter, diameter);
        const auto c = r.getCentre(); const float radius = diameter / 2;
        const float alpha = s.isEnabled() ? 1.0f : 0.32f;
        juce::Path track, active;
        track.addCentredArc (c.x, c.y, radius - 2, radius - 2, 0, start, end, true);
        active.addCentredArc (c.x, c.y, radius - 2, radius - 2, 0, start, start + value * (end - start), true);
        g.setColour (line.withAlpha (alpha)); g.strokePath (track, juce::PathStrokeType (2.5f));
        g.setColour (accent.withAlpha (alpha)); g.strokePath (active, juce::PathStrokeType (2.5f));
        g.setColour ((s.isMouseOverOrDragging() ? raised.brighter (0.12f) : raised).withAlpha (alpha)); g.fillEllipse (r.reduced (7));
        g.setColour ((s.hasKeyboardFocus (true) ? accent : line).withAlpha (alpha)); g.drawEllipse (r.reduced (7), 1);
        const float angle = start + value * (end - start);
        const auto p1 = c + juce::Point<float> (std::sin (angle), -std::cos (angle)) * (radius * 0.37f);
        const auto p2 = c + juce::Point<float> (std::sin (angle), -std::cos (angle)) * (radius * 0.63f);
        g.setColour (text.withAlpha (alpha)); g.drawLine ({ p1, p2 }, 2.5f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal)
        { juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, min, max, style, s); return; }
        const float alpha = s.isEnabled() ? 1.0f : 0.3f;
        const float cy = (float) y + (float) h / 2;
        g.setColour (line.withAlpha (alpha)); g.fillRect ((float) x, cy - 1, (float) w, 2.0f);
        g.setColour (accent.withAlpha (alpha)); g.fillRect ((float) x, cy - 1, juce::jmax (0.0f, pos - (float) x), 2.0f);
        g.setColour (text.withAlpha (alpha)); g.fillRoundedRectangle (pos - 3, cy - 7, 6, 14, 1.5f);
        if (s.hasKeyboardFocus (true)) { g.setColour (accent); g.drawRect (x, y, w, h, 1); }
    }

    void drawTableHeaderBackground (juce::Graphics& g, juce::TableHeaderComponent& h) override
    { g.fillAll (surface); g.setColour (line); g.drawHorizontalLine (h.getHeight() - 1, 0, (float) h.getWidth()); }
    void drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent&, const juce::String& name,
                                int, int w, int h, bool over, bool, int) override
    {
        if (over) { g.setColour (raised); g.fillAll(); }
        g.setFont (sattari::brand::fontOptions (11.0f)); g.setColour (muted);
        g.drawText (name, 10, 0, w - 16, h, juce::Justification::centredLeft);
    }
    int getTabButtonBestWidth (juce::TabBarButton&, int) override { return 150; }
    void drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool) override
    {
        g.fillAll (over ? raised : surface);
        g.setColour (b.isFrontTab() ? accent : muted); g.setFont (sattari::brand::fontOptions (12.0f, juce::Font::bold));
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
        if (b.isFrontTab()) { g.setColour (accent); g.fillRect (0, b.getHeight() - 2, b.getWidth(), 2); }
    }
private:
    juce::Colour accent;
    std::array<std::unique_ptr<juce::Drawable>, 6> icons;
};

class Header final : public juce::Component
{
public:
    Header (juce::String name, juce::String category, juce::Colour colour)
        : product (std::move (name)), categoryText (std::move (category)), accent (colour) { setTitle (product); }
    void setStatus (juce::String value, bool running)
    { if (status != value || active != running) { status = std::move (value); active = running; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (surface);
        auto r = getLocalBounds().reduced (22, 0);
        sattari::branding::drawLogo (g, r.removeFromLeft (86).withSizeKeepingCentre (82, 23).toFloat());
        r.removeFromLeft (22);
        g.setColour (line); g.fillRect (r.getX(), 20, 1, juce::jmax (1, getHeight() - 40)); r.removeFromLeft (20);
        auto state = r.removeFromRight (200);
        g.setColour (active ? accent : faint); g.fillEllipse ((float) state.getX(), (float) state.getCentreY() - 3, 6, 6);
        g.setFont (sattari::brand::fontOptions (11.0f)); g.drawText (status, state.withTrimmedLeft (14), juce::Justification::centredLeft);
        auto title = r.withSizeKeepingCentre (r.getWidth(), 46);
        g.setColour (text); g.setFont (sattari::brand::fontOptions (25.0f, juce::Font::bold));
        g.drawText (sattari::branding::compactProductName (product), title.removeFromTop (29), juce::Justification::centredLeft);
        g.setColour (muted); g.setFont (sattari::brand::fontOptions (10.5f)); g.drawText (categoryText, title, juce::Justification::centredLeft);
        g.setColour (line); g.fillRect (0, getHeight() - 1, getWidth(), 1);
        g.setColour (accent); g.fillRect (0, 0, 4, getHeight());
    }
private:
    juce::String product, categoryText, status;
    juce::Colour accent;
    bool active = false;
};

class MacroControl final : public juce::Component
{
public:
    MacroControl (juce::String name, juce::String suffix = {}, bool = false) : label (std::move (name))
    {
        control.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        control.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 82, 24);
        control.setTextValueSuffix (suffix); control.setTitle (label); control.setTooltip (label);
        control.setScrollWheelEnabled (false); addAndMakeVisible (control); setTitle (label);
    }
    juce::Slider& slider() { return control; }
    const juce::Slider& slider() const { return control; }
    void paint (juce::Graphics& g) override
    {
        g.setColour (isEnabled() ? muted : faint); g.setFont (sattari::brand::fontOptions (12.0f));
        g.drawFittedText (label, getLocalBounds().removeFromTop (21), juce::Justification::centred, 1);
    }
    void resized() override { control.setBounds (getLocalBounds().withTrimmedTop (21)); }
private:
    juce::String label;
    juce::Slider control;
};

class Metric final : public juce::Component
{
public:
    explicit Metric (juce::String title) : label (std::move (title)) { setTitle (label); }
    void setValue (juce::String value, juce::String detail = {})
    { if (value != number || detail != unit) { number = std::move (value); unit = std::move (detail); setDescription (label + " " + number + " " + unit); repaint(); } }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (8, 4);
        g.setColour (muted); g.setFont (sattari::brand::fontOptions (10.5f)); g.drawText (label, r.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (text); g.setFont (sattari::brand::fontOptions (20.0f)); g.drawFittedText (number.isEmpty() ? "--" : number, r.removeFromTop (27), juce::Justification::centredLeft, 1);
        g.setColour (faint); g.setFont (sattari::brand::fontOptions (10.0f)); g.drawText (unit, r, juce::Justification::centredLeft);
    }
private:
    juce::String label, number, unit;
};
} // namespace sattari::releaseui
