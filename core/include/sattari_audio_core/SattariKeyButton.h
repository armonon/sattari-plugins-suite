#pragma once

// =============================================================================
//  Sattari Audio Suite — shared "KeyButton" control
// -----------------------------------------------------------------------------
//  One tactile control for the whole key story, shared across every key-aware
//  plugin so they all behave identically. The face shows the key currently in
//  effect + how it is being driven (MANUAL / AUTO / FOLLOW); clicking opens a
//  menu to (optionally) link AutoKey, Follow Song Key from the master bus, or
//  set any root+scale by hand.
//
//  Originated in Auto Pitch; generalised here so Harmony / Maqam / VoxKey / Arp
//  reuse the exact same widget. It is configured (not subclassed) via KeyButton::
//  Config: the accent, which options to show, and the root/scale name lists — so
//  a diatonic plugin lists Major/Minor while a maqam plugin lists its maqamat,
//  all through one component.
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include "SattariBranding.h"
#include <functional>

namespace sattari::ui
{

class KeyButton final : public juce::Component
{
public:
    enum class Mode { Manual, Auto, Follow };

    struct Config
    {
        juce::String accentId  { "auto-pitch" };      // product id -> accent colour
        bool showAutoKey       = false;               // "Link AutoKey (detect from this track)"
        bool showFollow        = true;                // "Follow Song Key (from the master bus)"
        juce::String autoLabel   { "Link AutoKey  (auto-detect from this track)" };
    juce::String followLabel { "Follow session  (from Compass or Key)" };
        juce::StringArray rootNames  { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        juce::StringArray scaleNames { "major", "minor", "chromatic" };
        juce::String hint { "click: follow / set by hand" };
    };

    KeyButton()
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
        setAccessible (true);
        setTitle ("Musical key");
        setDescription ("Follow the suite key or choose a key manually");
    }

    void setConfig (Config c)
    {
        config = std::move (c);
        setDescription (config.hint);
        repaint();
    }

    // Callbacks (any may be left null). onManualKey passes a root index and a
    // scale index into config.scaleNames.
    std::function<void()> onLinkAuto;
    std::function<void()> onLinkFollow;
    std::function<void(int root, int scaleIdx)> onManualKey;

    void setState (Mode m, int root, int scaleIdx) noexcept
    {
        if (m == mode && root == rootIdx && scaleIdx == scaleIndex) return;
        mode = m; rootIdx = root; scaleIndex = scaleIdx; repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto accent = juce::Colour (sattari::brand::productAccent (config.accentId.toStdString()));
        const bool linked = (mode != Mode::Manual);
        auto r = getLocalBounds().toFloat().reduced (1.5f);

        auto body = juce::Colour (sattari::brand::kPanelRaised);
        if (pressed)     body = body.darker (0.30f);
        else if (hover)  body = body.brighter (0.14f);
        g.setColour (linked ? accent.withAlpha (pressed ? 0.30f : 0.16f) : body);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (linked ? accent.withAlpha (0.9f) : juce::Colour (sattari::brand::kHairline));
        g.drawRoundedRectangle (r, 10.0f, linked ? 1.8f : 1.0f);

        auto inner = r.reduced (12.0f, 9.0f);

        const char* chip = mode == Mode::Auto ? "AUTO-DETECT"
                         : mode == Mode::Follow ? "FOLLOWING SONG KEY" : "MANUAL KEY";
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (linked ? accent : juce::Colour (sattari::brand::kTextMuted));
        const juce::String chipText = (linked ? juce::String::fromUTF8 ("\xe2\x9b\x93  ") : juce::String()) + chip;
        g.drawText (chipText, inner.removeFromTop (14.0f), juce::Justification::centredLeft, false);

        const juce::String keyText = rootLabel (rootIdx) + "  " + scaleLabel (scaleIndex);
        g.setColour (juce::Colour (sattari::brand::kTextPrimary));
        g.setFont (juce::FontOptions (juce::jmin (28.0f, inner.getHeight() * 0.62f), juce::Font::bold));
        g.drawText (keyText, inner.removeFromTop (juce::jmax (18.0f, inner.getHeight() - 12.0f)),
                    juce::Justification::centredLeft, false);

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (juce::Colour (sattari::brand::kTextFaint));
        g.drawText (config.hint, inner, juce::Justification::bottomLeft, false);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        showMenu();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey
            || key == juce::KeyPress::spaceKey)
        {
            showMenu();
            return true;
        }
        return false;
    }

    void mouseUp    (const juce::MouseEvent&) override { pressed = false; repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    void showMenu()
    {
        pressed = true;
        repaint();
        juce::PopupMenu menu;
        if (config.showAutoKey) menu.addItem (1, config.autoLabel,   true, mode == Mode::Auto);
        if (config.showFollow)  menu.addItem (2, config.followLabel, true, mode == Mode::Follow);
        if (config.showAutoKey || config.showFollow) menu.addSeparator();

        juce::PopupMenu manual;
        const int nScales = config.scaleNames.size();
        for (int root = 0; root < config.rootNames.size(); ++root)
        {
            juce::PopupMenu sub;
            for (int s = 0; s < nScales; ++s)
                sub.addItem (kManualBase + root * nScales + s, config.scaleNames[s],
                             true, mode == Mode::Manual && rootIdx == root && scaleIndex == s);
            manual.addSubMenu (config.rootNames[root], sub);
        }
        menu.addSubMenu ("Set key manually", manual);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
            [this, nScales] (int id)
            {
                pressed = false; repaint();
                if (id == 1)            { if (onLinkAuto)   onLinkAuto(); }
                else if (id == 2)       { if (onLinkFollow) onLinkFollow(); }
                else if (id >= kManualBase && nScales > 0)
                {
                    const int idx = id - kManualBase;
                    if (onManualKey) onManualKey (idx / nScales, idx % nScales);
                }
            });
    }
    static constexpr int kManualBase = 100;

    juce::String rootLabel (int i) const
    {
        return juce::isPositiveAndBelow (i, config.rootNames.size()) ? config.rootNames[i] : juce::String (i);
    }
    juce::String scaleLabel (int i) const
    {
        return juce::isPositiveAndBelow (i, config.scaleNames.size()) ? config.scaleNames[i] : juce::String();
    }

    Config config;
    Mode mode = Mode::Manual;
    int  rootIdx = 0, scaleIndex = 0;
    bool hover = false, pressed = false;
};

} // namespace sattari::ui
