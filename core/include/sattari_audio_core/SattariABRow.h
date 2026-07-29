#pragma once

// =============================================================================
//  Sattari Audio Suite — reusable A/B compare row
// -----------------------------------------------------------------------------
//  A 3-button row (A | B | x) wired to a SattariPresetManager. First click on A
//  or B captures the current state into that slot; subsequent clicks recall it.
//  x swaps A <-> B in place. Editors drop one of these in next to the preset
//  combos with a single addAndMakeVisible() and a setBounds() in resized().
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include "SattariBranding.h"
#include "SattariPresetManager.h"

namespace sattari::ui
{

class SattariABRow final : public juce::Component
{
public:
    explicit SattariABRow (sattari::presets::SattariPresetManager& mgr)
        : manager (mgr)
    {
        auto stylePill = [] (juce::Button& button) {
            button.setColour (juce::TextButton::buttonColourId, juce::Colour (sattari::brand::kPanelRaised));
            button.setColour (juce::TextButton::textColourOnId, juce::Colour (sattari::brand::kTextPrimary));
            button.setColour (juce::TextButton::textColourOffId, juce::Colour (sattari::brand::kTextMuted));
        };
        stylePill (a); stylePill (b); stylePill (swap);
        a.setTitle ("Compare slot A");
        b.setTitle ("Compare slot B");
        swap.setTitle ("Swap compare slots");
        a.setDescription ("Capture or recall parameter state A");
        b.setDescription ("Capture or recall parameter state B");
        swap.setDescription ("Exchange parameter states A and B");

        a.onClick = [this] {
            if (active != 1 && manager.hasSlotA()) { manager.recallSlotA(); active = 1; }
            else { manager.captureSlotA(); active = 1; }
            updateSelection();
            repaint();
        };
        b.onClick = [this] {
            if (active != 2 && manager.hasSlotB()) { manager.recallSlotB(); active = 2; }
            else { manager.captureSlotB(); active = 2; }
            updateSelection();
            repaint();
        };
        swap.onClick = [this] { manager.swapSlots(); repaint(); };

        addAndMakeVisible (a);
        addAndMakeVisible (b);
        addAndMakeVisible (swap);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const int n = 3;
        const int gap = 2;
        const int w = (area.getWidth() - gap * (n - 1)) / n;
        a.setBounds    (area.removeFromLeft (w));
        area.removeFromLeft (gap);
        b.setBounds    (area.removeFromLeft (w));
        area.removeFromLeft (gap);
        swap.setBounds (area.removeFromLeft (w));
    }

private:
    void updateSelection()
    {
        a.setToggleState (active == 1, juce::dontSendNotification);
        b.setToggleState (active == 2, juce::dontSendNotification);
    }

    sattari::presets::SattariPresetManager& manager;
    juce::TextButton a { "A" }, b { "B" }, swap { "x" };
    int active = 0;
};

} // namespace sattari::ui
