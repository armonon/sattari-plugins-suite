#pragma once

#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariModuleInspector.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <sattari_audio_core/SattariRackProcessor.h>
#include <sattari_audio_core/SattariStudio.h>

#include <array>
#include <memory>
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
          abRow (presetManager),
          inspector (p.parameters())
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
            row.number.setText (number, juce::dontSendNotification);
            row.number.setTitle ("Slot " + juce::String (slot + 1));
            row.number.setJustificationType (juce::Justification::centred);
            row.number.setColour (
                juce::Label::textColourId, studio::colour::secondary);
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
        }

        duplicateNotice.setColour (
            juce::Label::textColourId, studio::colour::secondary);
        duplicateNotice.setFont (juce::FontOptions (10.5f));
        duplicateNotice.setJustificationType (juce::Justification::centredLeft);
        duplicateNotice.setAccessible (true);
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
        studio::paintCanvas (g, getLocalBounds(), accent);
        studio::paintSurface (g, chainArea.toFloat(), true);
        studio::paintSurface (g, inspectorArea.toFloat(), true);
        studio::paintSectionTitle (
            g, chainArea.reduced (16).removeFromTop (18),
            "Signal chain", "top to bottom");
        studio::paintSectionTitle (
            g, inspectorArea.reduced (16).removeFromTop (18),
            "Focused module",
            inspectorArea.getWidth() < 430
                ? juce::String ("automation")
                : juce::String ("automation-ready controls"));

        for (int slot = 0; slot < rack::SattariRackProcessor::kNumSlots; ++slot)
        {
            const auto& row = slots[(size_t) slot];
            const bool selected = slot == selectedSlot;
            const bool occupied = row.chooser.getSelectedId() > 1;

            g.setColour (selected ? accent.withAlpha (0.11f)
                                  : studio::colour::surfaceHigh);
            g.fillRoundedRectangle (row.bounds.toFloat(), 10.0f);
            g.setColour (selected ? accent.withAlpha (0.85f)
                                  : studio::colour::separator);
            g.drawRoundedRectangle (
                row.bounds.toFloat().reduced (0.5f), 10.0f,
                selected ? 1.5f : 1.0f);

            if (occupied)
            {
                auto dotArea = row.bounds;
                const auto dot = dotArea.removeFromLeft (5)
                                     .withSizeKeepingCentre (4, 4).toFloat();
                g.setColour (row.bypass.getToggleState()
                                 ? studio::colour::tertiary : accent);
                g.fillEllipse (dot);
            }

            if (slot + 1 < rack::SattariRackProcessor::kNumSlots)
            {
                const int x = row.bounds.getCentreX();
                const int y1 = row.bounds.getBottom() + 1;
                const int y2 = slots[(size_t) slot + 1].bounds.getY() - 1;
                g.setColour (studio::colour::separator);
                g.drawVerticalLine (x, (float) y1, (float) y2);
            }
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced ((int) studio::metric::outerMargin);
        header.setBounds (area.removeFromTop (studio::metric::headerHeight));

        auto command = area.removeFromTop (studio::metric::commandHeight);
        factoryPreset.setBounds (command.removeFromLeft (190));
        command.removeFromLeft (8);
        userPresets.setBounds (command.removeFromLeft (170));
        command.removeFromLeft (8);
        savePresetButton.setBounds (command.removeFromLeft (70));
        command.removeFromLeft (8);
        abRow.setBounds (command.removeFromLeft (94));

        area.removeFromTop ((int) studio::metric::gap);
        chainArea = area.removeFromLeft (
            juce::jlimit (370, 470, (int) std::round (getWidth() * 0.43)));
        area.removeFromLeft ((int) studio::metric::gap);
        inspectorArea = area;

        auto chain = chainArea.reduced (14);
        chain.removeFromTop (28);
        auto notice = chain.removeFromBottom (42);
        chain.removeFromBottom (6);
        const int rowGap = 6;
        const int rowHeight = juce::jlimit (
            32, 44,
            (chain.getHeight() - rowGap * (rack::SattariRackProcessor::kNumSlots - 1))
                / rack::SattariRackProcessor::kNumSlots);

        for (auto& row : slots)
        {
            row.bounds = chain.removeFromTop (rowHeight);
            auto controls = row.bounds.reduced (8, 5);
            row.number.setBounds (controls.removeFromLeft (34));
            controls.removeFromLeft (4);
            row.bypass.setBounds (controls.removeFromRight (96));
            controls.removeFromRight (6);
            row.chooser.setBounds (controls);
            chain.removeFromTop (rowGap);
        }
        duplicateNotice.setBounds (notice);

        auto focused = inspectorArea.reduced (14);
        focused.removeFromTop (28);
        inspectorViewport.setBounds (focused);
        inspector.setSize (
            juce::jmax (320, inspectorViewport.getWidth() - 10),
            inspector.getHeight());
        repaint();
    }

private:
    using ComboAttachment =
        juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment =
        juce::AudioProcessorValueTreeState::ButtonAttachment;

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
                presetManager.loadUserPreset (name);
                selectSlot (selectedSlot);
                updateHeaderStatus();
            }
        };
        addAndMakeVisible (userPresets);

        savePresetButton.setTitle ("Save rack preset");
        savePresetButton.setDescription ("Save the current chain and module controls");
        savePresetButton.onClick = [this]
        {
            const auto name = "User "
                + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
            if (presetManager.saveUserPreset (name))
            {
                refreshUserPresets();
                userPresets.setText (name, juce::dontSendNotification);
            }
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
        const int selection = (int) audioProcessor.parameters()
                                  .getRawParameterValue (
                                      rack::SattariRackProcessor::slotParamId (
                                          selectedSlot))->load();
        auto* module = audioProcessor.moduleAt (selection - 1);
        inspector.showModule (
            module != nullptr ? module->moduleId() : nullptr,
            module != nullptr ? module->displayName() : nullptr);

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
                    ? "Slot " + juce::String (selectedSlot + 1)
                        + " selected / edit its controls on the right."
                    : "Choose a module to add it to the signal path.",
            juce::dontSendNotification);
        resized();
    }

    void updateHeaderStatus()
    {
        int active = 0;
        for (const auto& row : slots)
            if (row.chooser.getSelectedId() > 1
                && ! row.bypass.getToggleState())
                ++active;
        header.setStatus (
            active > 0
                ? juce::String (active) + (active == 1 ? " MODULE ACTIVE"
                                                       : " MODULES ACTIVE")
                : "EMPTY CHAIN",
            active > 0);
        repaint();
    }

    rack::SattariRackProcessor& audioProcessor;
    const juce::Colour accent;
    studio::LookAndFeel lookAndFeel;
    studio::Header header;
    sattari::presets::SattariPresetManager presetManager;
    SattariABRow abRow;
    SattariModuleInspector inspector;
    juce::Viewport inspectorViewport;

    juce::ComboBox factoryPreset, userPresets;
    juce::TextButton savePresetButton { "Save" };
    juce::Label duplicateNotice;
    struct SlotRow
    {
        juce::Rectangle<int> bounds;
        juce::Label number;
        juce::ComboBox chooser;
        juce::ToggleButton bypass { "Bypass" };
        std::unique_ptr<ComboAttachment> chooserAtt;
        std::unique_ptr<ButtonAttachment> bypassAtt;
    };
    std::array<SlotRow, rack::SattariRackProcessor::kNumSlots> slots;

    juce::Rectangle<int> chainArea, inspectorArea;
    int selectedSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariRackEditor)
};

} // namespace sattari::ui
