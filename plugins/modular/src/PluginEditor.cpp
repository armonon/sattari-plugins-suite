#include "PluginEditor.h"
#include <sattari_audio_core/SattariLogo.h>
#include <sattari_audio_core/SattariReleaseUI.h>

namespace
{
using P = SattariModularProcessor;
using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
const juce::Colour ink (0xffebedef), muted (0xffa4aaa9), accent (0xffa9c3ac), base (0xff17191a);

void label (juce::Graphics& g, juce::String text, juce::Rectangle<int> r, float size = 11,
            juce::Colour colour = ink, juce::Justification align = juce::Justification::centredLeft)
{
    g.setFont (sattari::brand::fontOptions (size)); g.setColour (colour);
    g.drawFittedText (text, r, align, 1, 0.85f);
}

void screw (juce::Graphics& g, float x, float y)
{
    g.setColour (juce::Colour (0xff0c0d0e)); g.fillEllipse (x - 3, y - 3, 6, 6);
    g.setColour (juce::Colour (0xff7d8082)); g.drawEllipse (x - 3, y - 3, 6, 6, 0.7f);
    g.drawLine (x - 1.5f, y + 1.5f, x + 1.5f, y - 1.5f, 0.8f);
}

class AnalogLook final : public juce::LookAndFeel_V4
{
public:
    AnalogLook()
    {
        for (int id : std::initializer_list<int> { juce::Label::textColourId, juce::ComboBox::textColourId, juce::Slider::textBoxTextColourId,
                       juce::PopupMenu::textColourId, juce::TextEditor::textColourId }) setColour (id, ink);
        for (int id : std::initializer_list<int> { juce::ComboBox::backgroundColourId, juce::PopupMenu::backgroundColourId,
                       juce::Slider::textBoxBackgroundColourId, juce::TextEditor::backgroundColourId }) setColour (id, base);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3c4941));
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff656b68));
        setColour (juce::ToggleButton::textColourId, ink);
        setColour (juce::TooltipWindow::backgroundColourId, base);
        setColour (juce::TooltipWindow::textColourId, ink);
    }
    juce::Font getTextButtonFont (juce::TextButton& b, int h) override { return shared.getTextButtonFont (b, h); }
    juce::Font getComboBoxFont (juce::ComboBox& b) override { return shared.getComboBoxFont (b); }
    void positionComboBoxText (juce::ComboBox& b, juce::Label& l) override { shared.positionComboBoxText (b, l); }
    void drawComboBox (juce::Graphics& g, int w, int h, bool d, int x, int y, int bw, int bh, juce::ComboBox& b) override
    { shared.drawComboBox (g, w, h, d, x, y, bw, bh, b); }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool over, bool down) override
    { shared.drawButtonBackground (g, b, c, over, down); }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down) override
    { shared.drawButtonText (g, b, over, down); }
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool down) override
    { shared.drawToggleButton (g, b, over, down); }
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float value, float start, float end, juce::Slider& slider) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float diameter = juce::jmin (r.getWidth() - 12, r.getHeight() - 10, 60.0f);
        r = r.withSizeKeepingCentre (diameter, diameter);
        const auto centre = r.getCentre(); const float radius = diameter / 2;
        const bool silver = (bool) slider.getProperties()["silver"];
        const auto ticks = silver ? juce::Colour (0xff555d5e) : juce::Colour (0xffa0a3a0);
        for (int i = 0; i <= 10; ++i)
        {
            const float a = start + (end - start) * (float) i / 10;
            const juce::Point<float> v (std::sin (a), -std::cos (a));
            g.setColour (ticks); g.drawLine ({ centre + v * (radius + 1), centre + v * (radius + 3) }, 0.7f);
        }
        g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillEllipse (r.translated (1, 2));
        g.setGradientFill ({ juce::Colour (0xff737777), r.getTopLeft(), juce::Colour (0xff101213), r.getBottomRight(), false });
        g.fillEllipse (r);
        for (int i = 0; i < 32; ++i)
        {
            const float a = (float) i * juce::MathConstants<float>::twoPi / 32;
            const juce::Point<float> v (std::sin (a), -std::cos (a));
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawLine ({ centre + v * (radius - 3), centre + v * (radius - 0.7f) }, 1.1f);
        }
        auto face = r.reduced (4);
        g.setGradientFill ({ juce::Colour (0xff4b5050), face.getTopLeft(), juce::Colour (0xff202324), face.getBottomLeft(), false });
        g.fillEllipse (face); g.setColour (juce::Colour (0xff707676)); g.drawEllipse (face, 0.6f);
        const float a = start + value * (end - start);
        const juce::Point<float> v (std::sin (a), -std::cos (a));
        g.setColour (slider.isEnabled() ? juce::Colour (0xffede5cc) : muted);
        g.drawLine ({ centre + v * (radius * 0.25f), centre + v * (radius * 0.69f) }, 2.3f);
        if (slider.hasKeyboardFocus (true)) { g.setColour (accent); g.drawEllipse (r.expanded (4), 1); }
    }
private:
    sattari::releaseui::LookAndFeel shared { 0xffa9c3ac };
};

enum class Icon { folder, copy, remove, map, redo, stop };
class Tool final : public juce::DrawableButton
{
public:
    Tool (Icon icon, const juce::String& name) : DrawableButton (name, juce::DrawableButton::ImageOnButtonBackground)
    {
        // Lucide geometry. License: assets/icons/LICENSE.lucide.
        const char* paths[] = {
            R"(<path d="M20 20H4a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2Z"/>)",
            R"(<rect width="14" height="14" x="8" y="8" rx="2"/><path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"/>)",
            R"(<path d="m18 6-12 12M6 6l12 12"/>)",
            R"(<path d="M4 21v-7m0-4V3m8 18v-9m0-4V3m8 18v-5m0-4V3M1 14h6m2-6h6m2 8h6"/>)",
            R"(<path d="m15 14 5-5-5-5m5 5H9.5a5.5 5.5 0 0 0 0 11H13"/>)",
            R"(<circle cx="12" cy="12" r="10"/><rect x="9" y="9" width="6" height="6" rx="1"/>)"
        };
        auto xml = juce::parseXML (juce::String (R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#e9eded" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">)") + paths[(int) icon] + "</svg>");
        drawing = juce::Drawable::createFromSVG (*xml); setImages (drawing.get()); setEdgeIndent (7);
        setTooltip (name); setTitle (name); setColour (backgroundColourId, juce::Colour (0xff292d2e));
    }
private:
    std::unique_ptr<juce::Drawable> drawing;
};

struct Knob final : juce::Component
{
    Knob (P& processor, const juce::String& id, juce::String title, P::ControlInfo metadata = {})
        : p (processor), name (std::move (title)), info (std::move (metadata))
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 16);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setTitle (name); slider.setTooltip (name); slider.setScrollWheelEnabled (false);
        slider.setColour (juce::Slider::textBoxTextColourId, ink);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.onDragStart = [this] { p.captureUndo(); };
        addAndMakeVisible (slider);
        attachment = std::make_unique<Attachment> (p.parameters, id, slider);
        if (info.valid) slider.textFromValueFunction = [meta = info] (double v) { return meta.text ((float) v); };
        if (info.valid) slider.valueFromTextFunction = [meta = info] (const juce::String& v)
        { return (double) meta.range.convertTo0to1 (meta.range.snapToLegalValue (v.getFloatValue() / meta.displayScale)); };
        if (info.valid) slider.setDoubleClickReturnValue (true, info.initial);
        slider.updateText();
    }
    void resized() override
    {
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jlimit (28, 80, getWidth() - 12), 16);
        slider.setBounds (getLocalBounds().withTrimmedTop (15));
    }
    void paint (juce::Graphics& g) override
    { label (g, name, getLocalBounds().removeFromTop (15), 10, silver ? juce::Colour (0xff303737) : ink, juce::Justification::centred); }
    void metal (bool bright)
    {
        silver = bright; slider.getProperties().set ("silver", bright);
        slider.setColour (juce::Slider::textBoxTextColourId, bright ? juce::Colour (0xff262c2c) : ink);
        repaint();
    }
    P& p; juce::String name; P::ControlInfo info; bool silver = false;
    juce::Slider slider;
    std::unique_ptr<Attachment> attachment;
};

class MappingPanel final : public juce::Component
{
public:
    MappingPanel (P& p, int m) : processor (p), macro (m)
    {
        for (int i = 0; i < P::mappings; ++i)
        {
            auto& target = targets[(size_t) i]; target.addItem ("Unassigned", 1); target.setTitle ("Macro target " + juce::String (i + 1));
            for (int n = 0; n < P::nodes; ++n)
                for (int c = 0; c < P::controls; ++c)
                {
                    const auto info = p.info (p.nodeType (n), c); if (! info.valid) continue;
                    const auto location = n < 6 ? "Main " + juce::String (n + 1) : n < 8 ? "Return " + juce::String (n - 5) : "Layer " + juce::String (n - 7);
                    target.addItem (location + " / " + P::typeName (p.nodeType (n)) + " / " + info.name, n * P::controls + c + 2);
                }
            const auto suffix = juce::String (i + 1);
            target.setSelectedId ((int) p.value (P::macroId (m, "target" + suffix)) + 1, juce::dontSendNotification);
            target.onChange = [this, i, suffix]
            {
                processor.captureUndo(); const int chosen = targets[(size_t) i].getSelectedId() - 1;
                // One owner per parameter keeps the visible mapping unambiguous.
                if (chosen > 0) for (int mm = 0; mm < P::macros; ++mm) for (int map = 0; map < P::mappings; ++map)
                    if ((mm != macro || map != i) && (int) processor.value (P::macroId (mm, "target" + juce::String (map + 1))) == chosen)
                        processor.set (P::macroId (mm, "target" + juce::String (map + 1)), 0);
                processor.set (P::macroId (macro, "target" + suffix), (float) chosen); ++processor.uiRevision;
            };
            addAndMakeVisible (target);
            for (int j = 0; j < 2; ++j)
            {
                auto& control = limits[(size_t) (i * 2 + j)];
                control = std::make_unique<Knob> (p, P::macroId (m, (j == 0 ? "low" : "high") + suffix), j == 0 ? "Minimum" : "Maximum");
                control->slider.textFromValueFunction = [] (double v) { return juce::String (v * 100, 1) + "%"; };
                control->slider.valueFromTextFunction = [] (const juce::String& s) { return s.getDoubleValue() / 100.0; };
                control->slider.updateText();
                addAndMakeVisible (*control);
            }
        }
        setSize (400, 300);
    }
    void resized() override
    {
        for (int i = 0; i < 2; ++i)
        {
            const int y = 32 + i * 132; targets[(size_t) i].setBounds (14, y, 372, 28);
            limits[(size_t) (i * 2)]->setBounds (86, y + 30, 100, 92);
            limits[(size_t) (i * 2 + 1)]->setBounds (214, y + 30, 100, 92);
        }
    }
    void paint (juce::Graphics& g) override
    { g.fillAll (base); label (g, "MACRO " + juce::String (macro + 1) + " / ASSIGNMENTS", { 14, 6, 370, 20 }, 12, accent); }
private:
    P& processor; int macro;
    std::array<juce::ComboBox, 2> targets;
    std::array<std::unique_ptr<Knob>, 4> limits;
};

class RackRow final : public juce::Component
{
public:
    RackRow (P& processor, int index, std::function<void (int)> loadSample) : p (processor), node (index), import (std::move (loadSample))
    {
        for (int candidate = 0; candidate < P::typeCount; ++candidate) if (P::allowedType (node, candidate)) menu.addItem (P::typeName (candidate), candidate + 1);
        menu.onChange = [this] { p.selectModule (node, menu.getSelectedId() - 1); };
        addAndMakeVisible (menu);
        bypass.onClick = [this] { p.captureUndo(); p.set (P::nodeId (node, "bypass"), p.value (P::nodeId (node, "bypass")) < 0.5f ? 1.0f : 0.0f); };
        up.onClick = [this] { p.moveModule (node, -1); }; down.onClick = [this] { p.moveModule (node, 1); };
        copy.onClick = [this] { if (! p.duplicateModule (node)) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Lane full", "There is no empty slot in this lane."); };
        remove.onClick = [this] { p.selectModule (node, P::empty); };
        sample.onClick = [this] { import (node - P::effectSlots); };
        for (auto* c : std::initializer_list<juce::Component*> { &bypass, &up, &down, &copy, &remove, &sample }) addAndMakeVisible (*c);
        refresh();
    }
    bool refresh()
    {
        const int next = p.nodeType (node); const bool changed = next != type;
        if (changed)
        {
            type = next; knobs.clear(); discrete.clear(); switches.clear(); discreteLabels.clear();
            menu.setSelectedId (type + 1, juce::dontSendNotification);
            for (int c = 0; c < P::controls; ++c)
            {
                auto meta = p.info (type, c); if (! meta.valid) continue;
                if (meta.toggle)
                {
                    auto toggle = std::make_unique<juce::ToggleButton> (meta.name);
                    toggle->setTitle (meta.name); toggle->setTooltip (meta.name);
                    toggle->onClick = [this, c, ptr = toggle.get()]
                    { p.captureUndo(); p.set (P::controlId (node, c), ptr->getToggleState() ? 1.0f : 0.0f); };
                    addAndMakeVisible (*toggle); switches.emplace_back (c, std::move (toggle));
                }
                else if (! meta.choices.isEmpty())
                {
                    auto combo = std::make_unique<juce::ComboBox>();
                    combo->setTitle (meta.name); combo->setTooltip (meta.name);
                    combo->addItemList (meta.choices, 1);
                    combo->onChange = [this, c, meta, ptr = combo.get()]
                    { p.captureUndo(); p.set (P::controlId (node, c), meta.range.convertTo0to1 ((float) (ptr->getSelectedId() - 1))); };
                    addAndMakeVisible (*combo); discrete.emplace_back (c, std::move (combo));
                    discreteLabels.emplace_back (c, meta.name);
                }
                else
                {
                    auto knob = std::make_unique<Knob> (p, P::controlId (node, c), meta.name, meta);
                    knob->metal (type == P::clean || type == P::eq); addAndMakeVisible (*knob);
                    knobs.emplace_back (c, std::move (knob));
                }
            }
            sample.setVisible (type == P::granulator);
            for (auto* c : std::initializer_list<juce::Component*> { &bypass, &up, &down, &copy, &remove }) c->setVisible (type != P::empty);
            resized(); repaint();
        }
        bypass.setToggleState (p.value (P::nodeId (node, "bypass")) < 0.5f, juce::dontSendNotification);
        sample.setTooltip ("Source: " + p.sampleName (juce::jmax (0, node - P::effectSlots)));
        const int first = node < 6 ? 0 : node < 8 ? 6 : 8, last = node < 6 ? 5 : node < 8 ? 7 : 9;
        up.setEnabled (node > first); down.setEnabled (node < last);
        for (auto& knob : knobs)
        {
            const bool mapped = p.isMapped (node, knob.first);
            const bool syncedTime = type == P::echo && knob.first == 0 && p.value (P::controlId (node, 5)) >= 0.5f;
            knob.second->slider.setEnabled (! mapped && ! syncedTime);
            if (! knob.second->slider.isMouseButtonDown()) knob.second->slider.setValue (mapped ? p.effectiveValue (node, knob.first) : p.value (P::controlId (node, knob.first)), juce::dontSendNotification);
            knob.second->slider.setTooltip (mapped ? knob.second->name + " (macro assigned)" : syncedTime ? "Free Time (Tempo Sync enabled)" : knob.second->name);
        }
        for (auto& item : discrete)
        {
            const auto meta = p.info (type, item.first);
            item.second->setSelectedId (juce::roundToInt (meta.range.convertFrom0to1 (p.isMapped (node, item.first) ? p.effectiveValue (node, item.first) : p.value (P::controlId (node, item.first)))) + 1, juce::dontSendNotification);
            item.second->setEnabled (! p.isMapped (node, item.first));
        }
        for (auto& item : switches)
        {
            const bool mapped = p.isMapped (node, item.first);
            item.second->setToggleState ((mapped ? p.effectiveValue (node, item.first) : p.value (P::controlId (node, item.first))) >= 0.5f, juce::dontSendNotification);
            item.second->setEnabled (! mapped);
        }
        return changed;
    }
    int rowHeight() const { return type == P::empty ? 46 : 90; }
    void resized() override
    {
        menu.setBounds (22, 10, 132, 25);
        int x = 22; for (auto* b : std::initializer_list<juce::Component*> { &bypass, &up, &down, &copy, &remove }) { b->setBounds (x, 42, 25, 25); x += 27; }
        sample.setBounds (128, 68, 26, 20);
        const int count = controlCount(), width = juce::jmax (1, (getWidth() - 180) / juce::jmax (count, 1));
        for (auto& knob : knobs) knob.second->setBounds (170 + knob.first * width, 8, width, 78);
        for (auto& item : discrete) item.second->setBounds (174 + item.first * width, 40, width - 8, 26);
        for (auto& item : switches) item.second->setBounds (174 + item.first * width, 32, width - 8, 42);
    }
    void paint (juce::Graphics& g) override
    {
        const bool silver = type == P::clean || type == P::eq;
        const auto r = getLocalBounds().toFloat().reduced (1);
        const auto top = silver ? juce::Colour (0xffc7ccca) : type == P::heat ? juce::Colour (0xff292928) : juce::Colour (0xff404648);
        const auto bottom = silver ? juce::Colour (0xff989f9e) : juce::Colour (0xff24282a);
        g.setGradientFill ({ top, r.getTopLeft(), bottom, r.getBottomLeft(), false }); g.fillRoundedRectangle (r, 3);
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        for (int y = 3; y < getHeight(); y += 3) g.drawHorizontalLine (y, 2, (float) getWidth() - 2);
        g.setColour (juce::Colours::black); g.drawRoundedRectangle (r, 3, 1);
        screw (g, 10, 12); screw (g, (float) getWidth() - 10, 12);
        if (type != P::empty) { screw (g, 10, (float) getHeight() - 12); screw (g, (float) getWidth() - 10, (float) getHeight() - 12); }
        const auto text = silver ? juce::Colour (0xff303636) : muted;
        const auto location = node < 6 ? "SERIAL " + juce::String (node + 1) : node < 8 ? "RETURN " + juce::String (node - 5) : "SOURCE " + juce::String (node - 7);
        if (type != P::empty) label (g, location, { 24, 72, type == P::granulator ? 98 : 130, 14 }, 9, text);
        if (type == P::empty) label (g, location + " / EMPTY", { 180, 11, getWidth() - 220, 24 }, 10, text);
        const int width = juce::jmax (1, (getWidth() - 180) / juce::jmax (controlCount(), 1));
        for (auto& item : discreteLabels) label (g, item.second, { 170 + item.first * width, 8, width, 18 }, 10, text, juce::Justification::centred);
        if (type != P::empty)
        {
            g.setColour (p.value (P::nodeId (node, "bypass")) < 0.5f ? accent : juce::Colour (0xff565b56));
            g.fillEllipse (159, 17, 5, 5);
        }
    }
private:
    int controlCount() const { int count = 0; for (int c = 0; c < P::controls; ++c) if (p.info (type, c).valid) count = c + 1; return count; }
    P& p; int node, type = -1; std::function<void (int)> import;
    juce::ComboBox menu;
    sattari::releaseui::IconButton bypass { sattari::releaseui::Icon::power, "Module power" },
        up { sattari::releaseui::Icon::up, "Move up" }, down { sattari::releaseui::Icon::down, "Move down" };
    Tool copy { Icon::copy, "Duplicate module" }, remove { Icon::remove, "Remove module" }, sample { Icon::folder, "Load source sample" };
    std::vector<std::pair<int, std::unique_ptr<Knob>>> knobs;
    std::vector<std::pair<int, std::unique_ptr<juce::ComboBox>>> discrete;
    std::vector<std::pair<int, std::unique_ptr<juce::ToggleButton>>> switches;
    std::vector<std::pair<int, juce::String>> discreteLabels;
};

class VU final : public juce::Component
{
public:
    float level = 0; juce::String channel;
    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff080a09)); g.fillRoundedRectangle (bounds, 4);
        bounds.reduce (5, 5);
        g.setGradientFill ({ juce::Colour (0xffe8dfba), bounds.getTopLeft(), juce::Colour (0xffc9c6a2), bounds.getBottomLeft(), false });
        g.fillRoundedRectangle (bounds, 2);
        const juce::Point<float> origin (bounds.getCentreX(), bounds.getBottom() + 10);
        const float radius = bounds.getHeight() * 0.86f;
        for (int i = 0; i <= 10; ++i)
        {
            const float a = -0.8f + (float) i * 0.16f;
            const juce::Point<float> v (std::sin (a), -std::cos (a));
            g.setColour (i > 7 ? juce::Colour (0xff9d392e) : juce::Colour (0xff323c33));
            g.drawLine ({ origin + v * radius, origin + v * (radius - (i % 2 ? 5 : 9)) }, 1);
        }
        label (g, "-20    -10    -3   0   +3", { 14, 9, getWidth() - 28, 18 }, 9, juce::Colour (0xff444733), juce::Justification::centred);
        label (g, "VU  /  " + channel, { 0, 43, getWidth(), 20 }, 11, juce::Colour (0xff454632), juce::Justification::centred);
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float angle = -0.8f + juce::jlimit (0.0f, 1.0f, (db + 38.0f) / 24.0f) * 1.6f;
        g.saveState(); g.reduceClipRegion (bounds.toNearestInt());
        g.setColour (juce::Colour (0xff813c2a));
        g.drawLine ({ origin, origin + juce::Point<float> (std::sin (angle), -std::cos (angle)) * (radius - 3) }, 1.5f);
        g.restoreState();
    }
};
}

struct SattariModularEditor::View final : juce::Component, juce::Timer, juce::MidiKeyboardStateListener
{
    explicit View (P& processor) : p (processor), keyboard (keys, juce::MidiKeyboardComponent::horizontalKeyboard),
        output (p, "output", "OUTPUT"), ambience (p, "return", "RETURN"), tooltip (this, 500)
    {
        setLookAndFeel (&look);
        output.slider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
        output.slider.valueFromTextFunction = [] (const juce::String& v) { return v.getDoubleValue(); }; output.slider.updateText();
        ambience.slider.textFromValueFunction = [] (double v) { return juce::String (v * 100, 1) + "%"; };
        ambience.slider.valueFromTextFunction = [] (const juce::String& v) { return v.getDoubleValue() / 100; }; ambience.slider.updateText();
        keys.addListener (this); keyboard.setAvailableRange (36, 84); keyboard.setLowestVisibleKey (48);
        keyboard.setKeyWidth (25); keyboard.setWantsKeyboardFocus (true);
        addAndMakeVisible (keyboard);
        viewport.setViewedComponent (&rack, false); viewport.setScrollBarsShown (true, false); viewport.setScrollBarThickness (9);
        addAndMakeVisible (viewport);
        for (int n = 0; n < P::nodes; ++n)
        { rows[(size_t) n] = std::make_unique<RackRow> (p, n, [this] (int layer) { chooseFile (2, layer); }); rack.addAndMakeVisible (*rows[(size_t) n]); }
        for (int i = 0; i < 3; ++i)
        {
            modes[(size_t) i].setButtonText (juce::StringArray { "MIX", "DESIGN", "PLAY" }[i]);
            modes[(size_t) i].onClick = [this, i] { p.captureUndo(); p.set ("workspace", (float) i); keys.allNotesOff (0); ++p.uiRevision; };
            addAndMakeVisible (modes[(size_t) i]);
            scenes[(size_t) i].setButtonText (juce::String (i + 1)); scenes[(size_t) i].setTooltip ("Scene " + juce::String (i + 1));
            scenes[(size_t) i].onClick = [this, i] { p.selectScene (i); }; addAndMakeVisible (scenes[(size_t) i]);
        }
        for (int i = 0; i < p.getNumPrograms(); ++i) presets.addItem (p.getProgramName (i), i + 1);
        presets.onChange = [this] { if (presets.getSelectedId() > 0) p.loadFactory (presets.getSelectedId() - 1); };
        addAndMakeVisible (presets);
        save.onClick = [this] { chooseFile (1); }; load.onClick = [this] { chooseFile (0); };
        undo.onClick = [this] { p.undo(); }; redo.onClick = [this] { p.redo(); };
        a.setButtonText ("A"); b.setButtonText ("B"); a.onClick = [this] { p.selectAB (0); }; b.onClick = [this] { p.selectAB (1); };
        copy.onClick = [this] { p.copyAB(); };
        power.onClick = [this] { p.captureUndo(); p.set ("bypass", p.value ("bypass") < 0.5f ? 1 : 0); };
        panic.onClick = [this] { keys.allNotesOff (0); p.allNotesOff(); };
        clip.setButtonText ("CLIP"); clip.setTooltip ("Reset clip indicator"); clip.onClick = [this] { p.clipped.store (false); };
        for (auto* c : std::initializer_list<juce::Component*> { &save, &load, &undo, &redo, &a, &b, &copy, &power, &panic, &output, &ambience, &clip }) addAndMakeVisible (*c);
        meterL.channel = "L"; meterR.channel = "R"; addAndMakeVisible (meterL); addAndMakeVisible (meterR);
        for (int type = 1; type < P::typeCount; ++type)
        {
            auto button = std::make_unique<juce::TextButton> (P::typeName (type));
            button->setTooltip ("Add " + P::typeName (type));
            button->onClick = [this, type]
            {
                const int first = type <= P::image ? 0 : P::effectSlots, end = type <= P::image ? P::mainSlots : P::nodes;
                for (int n = first; n < end; ++n) if (p.nodeType (n) == P::empty) { p.selectModule (n, type); return; }
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Lane full", "Choose a module from a rack slot to replace it.");
            };
            addAndMakeVisible (*button); library.push_back (std::move (button));
        }
        for (int m = 0; m < P::macros; ++m)
        {
            const juce::String name = m == 0 ? "Warmth" : m == 1 ? "Space" : m == 2 ? "Movement" : "Macro " + juce::String (m + 1);
            macroKnobs[(size_t) m] = std::make_unique<Knob> (p, P::macroId (m), name);
            auto& knob = *macroKnobs[(size_t) m]; knob.slider.textFromValueFunction = [] (double v) { return juce::String (v * 100, 0) + "%"; };
            knob.slider.valueFromTextFunction = [] (const juce::String& s) { return s.getDoubleValue() / 100; }; knob.slider.updateText(); addAndMakeVisible (knob);
            mapButtons[(size_t) m] = std::make_unique<Tool> (Icon::map, "Assign " + name);
            mapButtons[(size_t) m]->onClick = [this, m]
            {
                mappingCallout.reset(); mappingContent.reset();
                mappingContent = std::make_unique<MappingPanel> (p, m);
                mappingCallout = std::make_unique<juce::CallOutBox> (*mappingContent, mapButtons[(size_t) m]->getBounds(), this);
                mappingCallout->enterModalState (true, nullptr, false);
            };
            addAndMakeVisible (*mapButtons[(size_t) m]);
        }
        refresh(); startTimerHz (25);
    }
    ~View() override
    { stopTimer(); mappingCallout.reset(); mappingContent.reset(); keys.allNotesOff (0); keys.removeListener (this); setLookAndFeel (nullptr); }
    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity) override { p.queueNote (note, velocity, true); }
    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override { p.queueNote (note, 0, false); }
    bool playing() const { return SATTARI_MODULAR_SYNTH || p.currentWorkspace() == 2; }
    void chooseFile (int operation, int layer = 0)
    {
        const auto title = operation == 1 ? "Save Modular preset" : operation == 2 ? "Load source sample" : "Load Modular preset";
        chooser = std::make_unique<juce::FileChooser> (title, juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), operation == 2 ? "*.wav;*.aif;*.aiff;*.flac" : "*.sattarimodular");
        auto flags = juce::FileBrowserComponent::canSelectFiles | (operation == 1 ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting : juce::FileBrowserComponent::openMode);
        chooser->launchAsync (flags, [safe = juce::Component::SafePointer<View> (this), operation, layer] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File()) return;
            auto file = fc.getResult(); if (operation == 1) file = file.withFileExtension ("sattarimodular");
            const auto result = operation == 1 ? safe->p.savePreset (file) : operation == 2 ? safe->p.loadSample (layer, file) : safe->p.loadPreset (file);
            if (result.failed()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Sattari Modular", result.getErrorMessage());
            safe->refresh();
        });
    }
    void refresh()
    {
        bool changed = false; for (auto& row : rows) changed = row->refresh() || changed;
        const bool showKeys = playing(); if (showKeys != keyboard.isVisible()) { keyboard.setVisible (showKeys); changed = true; }
        if (changed || lastRevision != p.uiRevision.load()) { lastRevision = p.uiRevision.load(); resized(); repaint(); }
        for (int i = 0; i < 3; ++i)
        { modes[(size_t) i].setToggleState (p.currentWorkspace() == i, juce::dontSendNotification); scenes[(size_t) i].setToggleState (p.selectedScene() == i, juce::dontSendNotification); }
        a.setToggleState (p.selectedAB() == 0, juce::dontSendNotification); b.setToggleState (p.selectedAB() == 1, juce::dontSendNotification);
        presets.setText (p.presetName(), juce::dontSendNotification);
        undo.setEnabled (p.canUndo()); redo.setEnabled (p.canRedo()); power.setToggleState (p.value ("bypass") < 0.5f, juce::dontSendNotification);
        clip.setToggleState (p.clipped.load(), juce::dontSendNotification);
    }
    void timerCallback() override
    {
        if (mappingCallout && ! mappingCallout->isCurrentlyModal()) { mappingCallout.reset(); mappingContent.reset(); }
        refresh(); meterL.level += (p.rms[0].load() - meterL.level) * 0.28f; meterR.level += (p.rms[1].load() - meterR.level) * 0.28f;
        meterL.repaint(); meterR.repaint(); repaint (getWidth() - 178, 342, 160, 60);
    }
    void resized() override
    {
        const int w = getWidth(), h = getHeight(), right = w - 180;
        for (int i = 0; i < 3; ++i) modes[(size_t) i].setBounds (w - 296 + i * 86, 24, 82, 30);
        presets.setBounds (154, 82, 230, 30); load.setBounds (392, 82, 30, 30); save.setBounds (426, 82, 30, 30);
        undo.setBounds (472, 82, 30, 30); redo.setBounds (506, 82, 30, 30);
        a.setBounds (554, 82, 30, 30); b.setBounds (588, 82, 30, 30); copy.setBounds (622, 82, 30, 30);
        for (int i = 0; i < 3; ++i) scenes[(size_t) i].setBounds (right - 122 + i * 35, 82, 31, 30);
        power.setBounds (w - 48, 82, 30, 30);
        panic.setBounds (w - 86, 82, 30, 30);
        for (int i = 0; i < (int) library.size(); ++i)
        { library[(size_t) i]->setBounds (20, 167 + i * 36 + (i >= 9 ? 24 : 0), 113, 29); library[(size_t) i]->setVisible (i < 9 || playing()); }
        const int footer = h - (playing() ? 206 : 136);
        viewport.setBounds (154, 128, right - 168, footer - 140);
        const int rw = viewport.getWidth() - 11; int y = 24;
        if (playing()) for (int n = 8; n < 10; ++n) { rows[(size_t) n]->setBounds (0, y, rw, rows[(size_t) n]->rowHeight()); y += rows[(size_t) n]->rowHeight() + 5; }
        for (int n = 8; n < 10; ++n) rows[(size_t) n]->setVisible (playing());
        if (playing()) y += 25;
        for (int n = 0; n < 8; ++n)
        { if (n == 6) y += 27; rows[(size_t) n]->setBounds (0, y, rw, rows[(size_t) n]->rowHeight()); y += rows[(size_t) n]->rowHeight() + 5; }
        rack.setSize (rw, y + 5);
        laneLabels.clear();
        auto addLane = [this] (juce::String name, int top)
        { auto l = std::make_unique<juce::Label>(); l->setText (name, juce::dontSendNotification); l->setFont (sattari::brand::fontOptions (10)); l->setColour (juce::Label::textColourId, muted); l->setBounds (3, top, 600, 20); rack.addAndMakeVisible (*l); laneLabels.push_back (std::move (l)); };
        if (playing()) addLane ("INSTRUMENT LAYERS", 0);
        addLane ("MAIN / SERIAL", rows[0]->getY() - 23); addLane ("AMBIENCE / PARALLEL RETURN", rows[6]->getY() - 23);
        meterL.setBounds (right + 8, 166, 152, 74); meterR.setBounds (right + 8, 250, 152, 74);
        clip.setBounds (right + 110, 348, 50, 25);
        output.setBounds (right + 28, 408, 110, 94); ambience.setBounds (right + 28, 514, 110, 94);
        const int macroWidth = (w - 184) / 8;
        for (int m = 0; m < 8; ++m)
        { macroKnobs[(size_t) m]->setBounds (154 + m * macroWidth, footer + 26, macroWidth - 25, 91); mapButtons[(size_t) m]->setBounds (154 + (m + 1) * macroWidth - 28, footer + 68, 24, 25); }
        keyboard.setBounds (154, h - 76, w - 174, 56);
    }
    void paint (juce::Graphics& g) override
    {
        const int w = getWidth(), h = getHeight(), right = w - 180, footer = h - (playing() ? 206 : 136);
        g.fillAll (base);
        g.setGradientFill ({ juce::Colour (0xff383d3e), 0, 0, juce::Colour (0xff202425), 0, 70, false }); g.fillRect (0, 0, w, 70);
        g.setColour (juce::Colours::white.withAlpha (0.024f)); for (int y = 1; y < 70; y += 3) g.drawHorizontalLine (y, 0, (float) w);
        g.setColour (juce::Colours::white); sattari::branding::drawLogo (g, { 24, 22, 126, 31 });
        label (g, "MODULAR", { 178, 12, 260, 35 }, 29);
        label (g, SATTARI_MODULAR_SYNTH ? "INSTRUMENT / ANALOG RACK" : "FX / ANALOG RACK", { 180, 47, 270, 14 }, 10, muted);
        screw (g, 10, 10); screw (g, (float) w - 10, 10);
        g.setColour (juce::Colour (0xff343a3a)); g.drawHorizontalLine (70, 0, (float) w); g.drawHorizontalLine (122, 0, (float) w);
        g.drawVerticalLine (144, 122, (float) footer); g.drawVerticalLine (right, 122, (float) footer);
        g.drawHorizontalLine (footer, 16, (float) w - 16);
        label (g, "MODULES", { 22, 135, 110, 22 }, 11, muted);
        label (g, "PRESET", { 22, 84, 110, 24 }, 11, muted);
        label (g, "SCENES", { right - 190, 84, 60, 24 }, 10, muted);
        label (g, "OUTPUT STAGE", { right + 12, 135, 150, 22 }, 11, muted);
        label (g, "0 VU = -18 dBFS", { right + 12, 324, 150, 18 }, 9, muted);
        label (g, "PEAK " + juce::String (juce::Decibels::gainToDecibels (juce::jmax (p.peak[0].load(), p.peak[1].load()), -90.0f), 1), { right + 12, 350, 92, 20 }, 10, muted);
        label (g, "DSP " + juce::String (p.cpu.load() * 100, 1) + "%", { right + 12, 380, 140, 18 }, 10, muted);
        label (g, "MACROS", { 22, footer + 26, 110, 24 }, 11, accent);
        label (g, "NATIVE ENGINE", { 22, h - 42, 120, 18 }, 9, accent);
        label (g, JucePlugin_VersionString " / CANDIDATE", { 22, h - 24, 124, 14 }, 9, muted);
    }
    P& p; AnalogLook look;
    juce::Viewport viewport; juce::Component rack;
    std::array<std::unique_ptr<RackRow>, P::nodes> rows;
    std::vector<std::unique_ptr<juce::Label>> laneLabels;
    std::vector<std::unique_ptr<juce::TextButton>> library;
    std::array<juce::TextButton, 3> modes, scenes;
    juce::ComboBox presets;
    sattari::releaseui::IconButton save { sattari::releaseui::Icon::save, "Save preset" }, undo { sattari::releaseui::Icon::undo, "Undo" }, power { sattari::releaseui::Icon::power, "Master power" };
    Tool load { Icon::folder, "Load preset" }, redo { Icon::redo, "Redo" }, copy { Icon::copy, "Copy current A/B to other" };
    Tool panic { Icon::stop, "All notes off" };
    juce::TextButton a, b, clip;
    juce::MidiKeyboardState keys; juce::MidiKeyboardComponent keyboard;
    Knob output, ambience; VU meterL, meterR;
    std::array<std::unique_ptr<Knob>, 8> macroKnobs;
    std::array<std::unique_ptr<Tool>, 8> mapButtons;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<MappingPanel> mappingContent;
    std::unique_ptr<juce::CallOutBox> mappingCallout;
    juce::TooltipWindow tooltip;
    unsigned lastRevision = 0;
};

SattariModularEditor::SattariModularEditor (SattariModularProcessor& p) : AudioProcessorEditor (p), view (std::make_unique<View> (p))
{ addAndMakeVisible (*view); setResizable (true, true); setResizeLimits (1120, 820, 1800, 1200); setSize (1440, 1000); }
SattariModularEditor::~SattariModularEditor() = default;
void SattariModularEditor::paint (juce::Graphics& g) { g.fillAll (base); }
void SattariModularEditor::resized() { view->setBounds (getLocalBounds()); }
