#pragma once

#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariModuleInspector.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <sattari_audio_core/SattariRackProcessor.h>
#include <sattari_audio_core/SattariReleaseUI.h>

#include <array>
#include <memory>
#include <set>
#include <string>

namespace sattari::ui
{

// =============================================================================
//  Studio effect-rack workspace
// -----------------------------------------------------------------------------
//  Mix, Create and Vocal are different module rosters inside the same serial
//  machine. The editor therefore presents one stable workflow:
//
//      command layer -> ordered chain -> focused module inspector.
//
//  Module parameters remain the processor's fixed APVTS contract. The UI only
//  changes which module is active in each slot and which inspector is visible.
// =============================================================================
class SattariRackEditor : public juce::AudioProcessorEditor
{
public:
    SattariRackEditor (rack::SattariRackProcessor& p,
                       const juce::String& productName,
                       const juce::String& category,
                       const std::string& brandSlug)
        : juce::AudioProcessorEditor (&p),
          audioProcessor (p),
          accent (sattari::brand::productAccent (brandSlug)),
          lookAndFeel (sattari::brand::productAccent (brandSlug)),
          header (productName, category, accent),
          presetManager (p.parameters(), juce::String (brandSlug)),
          abRow (presetManager, true),
          inspector (p.parameters(), true)
    {
        setLookAndFeel (&lookAndFeel);
        setTitle (productName);
        setDescription (category + " with an ordered eight-slot module chain");
        setResizable (true, true);
        setResizeLimits (760, 560, 1440, 960);
        setSize (1080, 660);

        addAndMakeVisible (header);
        setupPresets();

        const auto menu = audioProcessor.moduleMenuNames();
        for (int slot = 0; slot < rack::SattariRackProcessor::kNumSlots; ++slot)
        {
            auto& row = slots[(size_t) slot];
            const auto number = juce::String (slot + 1).paddedLeft ('0', 2);
            row.number.setButtonText (number);
            row.number.setTitle ("Slot " + juce::String (slot + 1));
            row.number.setTooltip ("Select slot " + juce::String (slot + 1));
            row.number.onClick = [this, slot] { selectSlot (slot); };
            addAndMakeVisible (row.number);

            row.chooser.addItemList (menu, 1);
            row.chooser.setTitle ("Slot " + juce::String (slot + 1) + " module");
            row.chooser.setDescription ("Choose the module processed in this slot");
            addAndMakeVisible (row.chooser);
            row.chooserAtt = std::make_unique<ComboAttachment> (
                audioProcessor.parameters(),
                rack::SattariRackProcessor::slotParamId (slot),
                row.chooser);

            row.bypass.setTitle ("Slot " + juce::String (slot + 1) + " bypass");
            row.bypass.setDescription ("Temporarily bypass this rack slot");
            releaseui::icon (row.bypass, releaseui::Icon::power, "Bypass slot " + juce::String (slot + 1));
            row.bypass.getProperties().set ("releaseInvertToggle", true);
            row.bypass.setClickingTogglesState (true);
            addAndMakeVisible (row.bypass);
            row.bypassAtt = std::make_unique<ButtonAttachment> (
                audioProcessor.parameters(),
                rack::SattariRackProcessor::slotBypassParamId (slot),
                row.bypass);

            row.chooser.onChange = [this, slot]
            {
                selectSlot (slot);
                updateHeaderStatus();
            };
            row.bypass.onStateChange = [this] { updateHeaderStatus(); };
            releaseui::icon (row.up, releaseui::Icon::up, "Move slot " + juce::String (slot + 1) + " up");
            releaseui::icon (row.down, releaseui::Icon::down, "Move slot " + juce::String (slot + 1) + " down");
            row.up.onClick = [this, slot] { moveSlot (slot, slot - 1); };
            row.down.onClick = [this, slot] { moveSlot (slot, slot + 1); };
            row.up.setEnabled (slot > 0); row.down.setEnabled (slot + 1 < rack::SattariRackProcessor::kNumSlots);
            addAndMakeVisible (row.up); addAndMakeVisible (row.down);
        }

        duplicateNotice.setColour (
            juce::Label::textColourId, releaseui::muted);
        duplicateNotice.setFont (sattari::brand::fontOptions (10.5f));
        duplicateNotice.setJustificationType (juce::Justification::centredLeft);
        duplicateNotice.setAccessible (true);
        duplicateNotice.setTitle ("Chain compatibility notice");
        addAndMakeVisible (duplicateNotice);

        inspectorViewport.setViewedComponent (&inspector, false);
        inspectorViewport.setScrollBarsShown (true, false);
        inspectorViewport.setScrollBarThickness (8);
        inspectorViewport.setTitle ("Selected module controls");
        inspectorViewport.setDescription (
            "Automation controls for the selected rack module");
        addAndMakeVisible (inspectorViewport);

        int firstSelected = 0;
        for (int slot = 0; slot < rack::SattariRackProcessor::kNumSlots; ++slot)
            if (audioProcessor.parameters().getRawParameterValue (
                    rack::SattariRackProcessor::slotParamId (slot))->load() > 0.0f)
            {
                firstSelected = slot;
                break;
            }
        selectSlot (firstSelected);
        updateHeaderStatus();
    }

    ~SattariRackEditor() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (releaseui::canvas);
        g.setColour (releaseui::surface); g.fillRect (chainArea);
        g.setColour (releaseui::line);
        g.drawHorizontalLine (124, 0, (float) getWidth());
        g.drawVerticalLine (chainArea.getRight(), (float) chainArea.getY(), (float) chainArea.getBottom());
        releaseui::section (g, chainArea.reduced (18, 12).removeFromTop (22), "Signal chain", "8 slots");

        for (int slot = 0; slot < rack::SattariRackProcessor::kNumSlots; ++slot)
        {
            const auto& row = slots[(size_t) slot];
            const bool selected = slot == selectedSlot;
            const bool occupied = row.chooser.getSelectedId() > 1;

            g.setColour (selected ? accent.withAlpha (0.10f) : releaseui::surface);
            g.fillRect (row.bounds);
            g.setColour (releaseui::line.withAlpha (0.6f));
            g.drawHorizontalLine (row.bounds.getBottom() - 1, (float) row.bounds.getX(), (float) row.bounds.getRight());
            if (selected) { g.setColour (accent); g.fillRect (row.bounds.getX(), row.bounds.getY(), 2, row.bounds.getHeight()); }

            if (occupied)
            {
                auto dotArea = row.bounds;
                const auto dot = dotArea.removeFromLeft (5)
                                     .withSizeKeepingCentre (4, 4).toFloat();
                g.setColour (row.bypass.getToggleState()
                                 ? releaseui::faint : accent);
                g.fillEllipse (dot);
            }

            if (slot + 1 < rack::SattariRackProcessor::kNumSlots)
            {
                const int x = row.bounds.getCentreX();
                const int y1 = row.bounds.getBottom() + 1;
                const int y2 = slots[(size_t) slot + 1].bounds.getY() - 1;
                g.setColour (releaseui::line);
                g.drawVerticalLine (x, (float) y1, (float) y2);
            }
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        header.setBounds (area.removeFromTop (72));
        auto command = area.removeFromTop (52).reduced (22, 9);
        factoryPreset.setBounds (command.removeFromLeft (190));
        command.removeFromLeft (8);
        userPresets.setBounds (command.removeFromLeft (170));
        command.removeFromLeft (8);
        savePresetButton.setBounds (command.removeFromLeft (34));
        command.removeFromLeft (8);
        abRow.setBounds (command.removeFromLeft (108));
        chainArea = area.removeFromLeft (
            juce::jlimit (330, 410, (int) std::round (getWidth() * 0.35)));
        inspectorArea = area;

        auto chain = chainArea.reduced (14);
        chain.removeFromTop (36);
        auto notice = chain.removeFromBottom (42);
        chain.removeFromBottom (6);
        const int rowGap = 2;
        const int rowHeight = juce::jlimit (
            32, 54,
            (chain.getHeight() - rowGap * (rack::SattariRackProcessor::kNumSlots - 1))
                / rack::SattariRackProcessor::kNumSlots);

        for (auto& row : slots)
        {
            row.bounds = chain.removeFromTop (rowHeight);
            auto controls = row.bounds.reduced (8, 5);
            row.number.setBounds (controls.removeFromLeft (26));
            controls.removeFromLeft (4);
            row.down.setBounds (controls.removeFromRight (22)); controls.removeFromRight (2);
            row.up.setBounds (controls.removeFromRight (22)); controls.removeFromRight (4);
            row.bypass.setBounds (controls.removeFromRight (28));
            controls.removeFromRight (6);
            row.chooser.setBounds (controls);
            chain.removeFromTop (rowGap);
        }
        duplicateNotice.setBounds (notice);

        auto focused = inspectorArea.reduced (14);
        inspectorViewport.setBounds (focused);
        inspector.setSize (
            juce::jmax (1, inspectorViewport.getWidth() - 10),
            inspector.getHeight());
        repaint();
    }

private:
    using ComboAttachment =
        juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment =
        juce::AudioProcessorValueTreeState::ButtonAttachment;

    void moveSlot (int from, int to)
    {
        if (to < 0 || to >= rack::SattariRackProcessor::kNumSlots) return;
        auto& state = audioProcessor.parameters();
        for (bool bypass : { false, true })
        {
            auto* a = state.getParameter (bypass ? rack::SattariRackProcessor::slotBypassParamId (from) : rack::SattariRackProcessor::slotParamId (from));
            auto* b = state.getParameter (bypass ? rack::SattariRackProcessor::slotBypassParamId (to) : rack::SattariRackProcessor::slotParamId (to));
            const float av = a->getValue(), bv = b->getValue();
            a->beginChangeGesture(); b->beginChangeGesture();
            a->setValueNotifyingHost (bv); b->setValueNotifyingHost (av);
            a->endChangeGesture(); b->endChangeGesture();
        }
        selectSlot (to); updateHeaderStatus();
    }

    void setupPresets()
    {
        factoryPreset.setTitle ("Factory preset");
        factoryPreset.setDescription ("Load a complete rack wiring preset");
        for (int i = 0; i < audioProcessor.getNumPrograms(); ++i)
        {
            auto name = audioProcessor.getProgramName (i);
            if (name.isEmpty()) name = "Initial state";
            factoryPreset.addItem (name, i + 1);
        }
        factoryPreset.setSelectedId (
            audioProcessor.getCurrentProgram() + 1, juce::dontSendNotification);
        factoryPreset.onChange = [this]
        {
            const int program = factoryPreset.getSelectedItemIndex();
            if (program >= 0)
            {
                audioProcessor.setCurrentProgram (program);
                selectSlot (selectedSlot);
                updateHeaderStatus();
            }
        };
        addAndMakeVisible (factoryPreset);

        userPresets.setTitle ("User presets");
        userPresets.setDescription ("Recall a saved rack state");
        userPresets.setTextWhenNothingSelected ("User presets");
        userPresets.onChange = [this]
        {
            const auto name = userPresets.getText();
            if (name.isNotEmpty())
            {
                if (! presetManager.loadUserPreset (name))
                {
                    userPresets.setSelectedId (0, juce::dontSendNotification);
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                        "Could not load preset", "The preset is missing, unreadable, invalid, or belongs to another product. Your current settings have not changed.");
                    return;
                }
                selectSlot (selectedSlot);
                updateHeaderStatus();
            }
        };
        addAndMakeVisible (userPresets);

        releaseui::icon (savePresetButton, releaseui::Icon::save, "Save rack preset");
        savePresetButton.setDescription ("Save the current chain and module controls");
        savePresetButton.onClick = [this]
        {
            const auto name = presetManager.newUserPresetName();
            if (presetManager.saveUserPreset (name))
            {
                refreshUserPresets();
                userPresets.setText (name, juce::dontSendNotification);
            }
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    "Could not save preset", "Check that your preset folder is writable and that the disk has free space. Your current settings remain active.");
        };
        addAndMakeVisible (savePresetButton);
        addAndMakeVisible (abRow);
        refreshUserPresets();
    }

    void refreshUserPresets()
    {
        userPresets.clear (juce::dontSendNotification);
        int id = 1;
        for (const auto& name : presetManager.userPresetNames())
            userPresets.addItem (name, id++);
    }

    void selectSlot (int slot)
    {
        selectedSlot = juce::jlimit (
            0, rack::SattariRackProcessor::kNumSlots - 1, slot);
        for (int i = 0; i < rack::SattariRackProcessor::kNumSlots; ++i)
            slots[(size_t) i].number.setToggleState (i == selectedSlot, juce::dontSendNotification);
        const int selection = (int) audioProcessor.parameters()
                                  .getRawParameterValue (
                                      rack::SattariRackProcessor::slotParamId (
                                          selectedSlot))->load();
        auto* module = audioProcessor.moduleAt (selection - 1);
        inspector.showModule (
            module != nullptr ? module->moduleId() : nullptr,
            module != nullptr ? module->displayName() : nullptr);

        updateNotice();
        resized();
    }

    void updateNotice()
    {
        const int selection = (int) audioProcessor.parameters().getRawParameterValue (
            rack::SattariRackProcessor::slotParamId (selectedSlot))->load();
        auto* module = audioProcessor.moduleAt (selection - 1);

        bool duplicate = false;
        if (selection > 0)
            for (int earlier = 0; earlier < selectedSlot; ++earlier)
                duplicate = duplicate
                    || ((int) audioProcessor.parameters().getRawParameterValue (
                            rack::SattariRackProcessor::slotParamId (earlier))->load()
                        == selection
                        && audioProcessor.parameters().getRawParameterValue (
                            rack::SattariRackProcessor::slotBypassParamId (earlier))->load()
                           <= 0.5f);

        duplicateNotice.setText (
            duplicate
                ? "This duplicate is inactive; the first active slot owns its DSP state."
                : module != nullptr
                    ? "Slot " + juce::String (selectedSlot + 1) + " / " + juce::String (module->displayName())
                    : "Slot " + juce::String (selectedSlot + 1) + " / Empty",
            juce::dontSendNotification);
    }

    void updateHeaderStatus()
    {
        std::set<int> activeModules;
        for (const auto& row : slots)
            if (row.chooser.getSelectedId() > 1
                && ! row.bypass.getToggleState())
                activeModules.insert (row.chooser.getSelectedId());
        const auto active = activeModules.size();
        header.setStatus (
            active > 0
                ? juce::String (active) + (active == 1 ? " MODULE ACTIVE"
                                                       : " MODULES ACTIVE")
                : "EMPTY CHAIN",
            active > 0);
        updateNotice();
        repaint();
    }

    rack::SattariRackProcessor& audioProcessor;
    const juce::Colour accent;
    releaseui::LookAndFeel lookAndFeel;
    releaseui::Header header;
    sattari::presets::SattariPresetManager presetManager;
    SattariABRow abRow;
    SattariModuleInspector inspector;
    juce::Viewport inspectorViewport;

    juce::ComboBox factoryPreset, userPresets;
    juce::TextButton savePresetButton { "Save preset" };
    juce::Label duplicateNotice;
    struct SlotRow
    {
        juce::Rectangle<int> bounds;
        juce::TextButton number;
        juce::ComboBox chooser;
        juce::TextButton bypass;
        juce::TextButton up, down;
        std::unique_ptr<ComboAttachment> chooserAtt;
        std::unique_ptr<ButtonAttachment> bypassAtt;
    };
    std::array<SlotRow, rack::SattariRackProcessor::kNumSlots> slots;

    juce::Rectangle<int> chainArea, inspectorArea;
    int selectedSlot = 0;
    juce::TooltipWindow tooltip { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariRackEditor)
};

} // namespace sattari::ui
