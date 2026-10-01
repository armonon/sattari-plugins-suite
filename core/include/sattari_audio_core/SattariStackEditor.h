#pragma once

#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariInstrumentRack.h>
#include <sattari_audio_core/SattariModuleInspector.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <sattari_audio_core/SattariStudio.h>

#include <array>
#include <memory>
#include <string>

namespace sattari::ui
{

// =============================================================================
//  Studio instrument-rack workspace
// -----------------------------------------------------------------------------
//  Stack is parallel rather than serial, so every row is a complete playable
//  layer: instrument, mute, level, pan, key range and transpose. The inspector
//  focuses the selected instrument type while source actions remain attached to
//  the selected layer.
// =============================================================================
class SattariStackEditor : public juce::AudioProcessorEditor,
                           private juce::ChangeListener
{
public:
    SattariStackEditor (rack::SattariInstrumentRack& p,
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
        using InstrumentRack = rack::SattariInstrumentRack;

        setLookAndFeel (&lookAndFeel);
        setTitle (productName);
        setDescription (category + " with four parallel instrument layers");
        setResizable (true, true);
        setResizeLimits (1160, 620, 1680, 1040);
        setSize (1280, 720);

        addAndMakeVisible (header);
        setupPresets();

        const auto menu = audioProcessor.moduleMenuNames();
        for (int layer = 0; layer < InstrumentRack::kNumLayers; ++layer)
        {
            auto& row = layers[(size_t) layer];
            row.number.setText (
                juce::String (layer + 1).paddedLeft ('0', 2),
                juce::dontSendNotification);
            row.number.setTitle ("Layer " + juce::String (layer + 1));
            row.number.setJustificationType (juce::Justification::centred);
            row.number.setColour (
                juce::Label::textColourId, studio::colour::secondary);
            addAndMakeVisible (row.number);

            row.chooser.addItemList (menu, 1);
            row.chooser.setTitle (
                "Layer " + juce::String (layer + 1) + " instrument");
            row.chooser.setDescription ("Choose the instrument for this layer");
            addAndMakeVisible (row.chooser);
            row.chooserAtt = std::make_unique<ComboAttachment> (
                audioProcessor.parameters(),
                InstrumentRack::layerParamId (layer), row.chooser);

            row.mute.setTitle ("Layer " + juce::String (layer + 1) + " mute");
            row.mute.setDescription ("Mute this instrument layer");
            addAndMakeVisible (row.mute);
            row.muteAtt = std::make_unique<ButtonAttachment> (
                audioProcessor.parameters(),
                InstrumentRack::layerMuteParamId (layer), row.mute);

            setupLayerSlider (row.level, row.levelLabel, "Level", " dB");
            setupLayerSlider (row.pan, row.panLabel, "Pan");
            setupLayerSlider (row.low, row.lowLabel, "Low key");
            setupLayerSlider (row.high, row.highLabel, "High key");
            setupLayerSlider (row.transpose, row.transposeLabel, "Transpose", " st");

            row.levelAtt = std::make_unique<SliderAttachment> (
                audioProcessor.parameters(),
                InstrumentRack::layerLevelParamId (layer), row.level);
            row.panAtt = std::make_unique<SliderAttachment> (
                audioProcessor.parameters(),
                InstrumentRack::layerPanParamId (layer), row.pan);
            row.lowAtt = std::make_unique<SliderAttachment> (
                audioProcessor.parameters(),
                InstrumentRack::layerLoParamId (layer), row.low);
            row.highAtt = std::make_unique<SliderAttachment> (
                audioProcessor.parameters(),
                InstrumentRack::layerHiParamId (layer), row.high);
            row.transposeAtt = std::make_unique<SliderAttachment> (
                audioProcessor.parameters(),
                InstrumentRack::layerTransParamId (layer), row.transpose);

            row.chooser.onChange = [this, layer]
            {
                selectLayer (layer);
                updateHeaderStatus();
            };
            row.mute.onStateChange = [this] { updateHeaderStatus(); };
        }

        sourceA.setButtonText ("Load source A");
        sourceA.setTitle ("Load source A");
        sourceA.setDescription (
            "Load audio into the selected Chop or Granulator layer");
        sourceA.onClick = [this] { chooseSource (0); };
        addAndMakeVisible (sourceA);

        sourceB.setButtonText ("Load source B");
        sourceB.setTitle ("Load source B");
        sourceB.setDescription ("Load audio into Granulator source B");
        sourceB.onClick = [this] { chooseSource (1); };
        addAndMakeVisible (sourceB);

        sourceStatus.setColour (
            juce::Label::textColourId, studio::colour::secondary);
        sourceStatus.setFont (sattari::brand::fontOptions (10.5f));
        sourceStatus.setJustificationType (juce::Justification::centredLeft);
        sourceStatus.setAccessible (true);
        addAndMakeVisible (sourceStatus);

        inspectorViewport.setViewedComponent (&inspector, false);
        inspectorViewport.setScrollBarsShown (true, false);
        inspectorViewport.setScrollBarThickness (8);
        inspectorViewport.setTitle ("Selected instrument controls");
        inspectorViewport.setDescription (
            "Shared automation controls for the selected instrument type");
        addAndMakeVisible (inspectorViewport);

        int firstSelected = 0;
        for (int layer = 0; layer < InstrumentRack::kNumLayers; ++layer)
            if (audioProcessor.parameters().getRawParameterValue (
                    InstrumentRack::layerParamId (layer))->load() > 0.0f)
            {
                firstSelected = layer;
                break;
            }
        selectLayer (firstSelected);
        updateHeaderStatus();
        audioProcessor.addChangeListener (this);
    }

    ~SattariStackEditor() override
    {
        audioProcessor.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        studio::paintCanvas (g, getLocalBounds(), accent);
        studio::paintSurface (g, layerArea.toFloat(), true);
        studio::paintSurface (g, inspectorArea.toFloat(), true);
        studio::paintSectionTitle (
            g, layerArea.reduced (16).removeFromTop (18),
            "Instrument layers", "parallel / phase-aligned");
        studio::paintSectionTitle (
            g, inspectorArea.reduced (16).removeFromTop (18),
            "Focused instrument", "shared controls");

        for (int layer = 0;
             layer < rack::SattariInstrumentRack::kNumLayers;
             ++layer)
        {
            const auto& row = layers[(size_t) layer];
            const bool selected = layer == selectedLayer;
            const bool occupied = row.chooser.getSelectedId() > 1;
            g.setColour (selected ? accent.withAlpha (0.11f)
                                  : studio::colour::surfaceHigh);
            g.fillRoundedRectangle (row.bounds.toFloat(), 11.0f);
            g.setColour (selected ? accent.withAlpha (0.88f)
                                  : studio::colour::separator);
            g.drawRoundedRectangle (
                row.bounds.toFloat().reduced (0.5f), 11.0f,
                selected ? 1.5f : 1.0f);

            if (occupied)
            {
                auto dotArea = row.bounds;
                const auto dot = dotArea.removeFromLeft (5)
                                     .withSizeKeepingCentre (4, 4).toFloat();
                g.setColour (row.mute.getToggleState()
                                 ? studio::colour::tertiary : accent);
                g.fillEllipse (dot);
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
        abRow.setBounds (command.removeFromLeft (124));

        area.removeFromTop ((int) studio::metric::gap);
        layerArea = area.removeFromLeft (
            juce::jlimit (680, 870, (int) std::round (getWidth() * 0.65)));
        area.removeFromLeft ((int) studio::metric::gap);
        inspectorArea = area;

        auto layerPanel = layerArea.reduced (14);
        layerPanel.removeFromTop (28);
        auto sourcePanel = layerPanel.removeFromBottom (62);
        layerPanel.removeFromBottom (8);
        const int rowGap = 8;
        const int rowHeight = juce::jlimit (
            68, 104,
            (layerPanel.getHeight()
                - rowGap * (rack::SattariInstrumentRack::kNumLayers - 1))
                / rack::SattariInstrumentRack::kNumLayers);

        for (auto& row : layers)
        {
            row.bounds = layerPanel.removeFromTop (rowHeight);
            auto controls = row.bounds.reduced (8, 8);
            row.number.setBounds (controls.removeFromLeft (32));
            controls.removeFromLeft (4);
            row.chooser.setBounds (
                controls.removeFromLeft (132).withSizeKeepingCentre (132, 32));
            controls.removeFromLeft (6);
            row.mute.setBounds (
                controls.removeFromLeft (90).withSizeKeepingCentre (90, 28));
            controls.removeFromLeft (6);

            auto place = [&controls, rowHeight] (
                juce::Label& label, juce::Slider& slider, int width)
            {
                auto cell = controls.removeFromLeft (width);
                label.setBounds (cell.removeFromTop (16));
                slider.setBounds (
                    rowHeight >= 84 ? cell.removeFromTop (32) : cell);
                controls.removeFromLeft (4);
            };
            place (row.levelLabel, row.level, 92);
            place (row.panLabel, row.pan, 82);
            place (row.lowLabel, row.low, 68);
            place (row.highLabel, row.high, 68);
            place (row.transposeLabel, row.transpose, controls.getWidth());
            layerPanel.removeFromTop (rowGap);
        }

        auto sourceLine = sourcePanel.removeFromTop (34);
        sourceA.setBounds (sourceLine.removeFromLeft (130));
        sourceLine.removeFromLeft (8);
        sourceB.setBounds (sourceLine.removeFromLeft (130));
        sourceStatus.setBounds (sourcePanel);

        auto focused = inspectorArea.reduced (14);
        focused.removeFromTop (28);
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
    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;

    void setupPresets()
    {
        factoryPreset.setTitle ("Factory preset");
        factoryPreset.setDescription ("Load a complete instrument stack");
        for (int i = 0; i < audioProcessor.getNumPrograms(); ++i)
        {
            auto name = audioProcessor.getProgramName (i);
            if (name.isEmpty()) name = "Initial stack";
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
                selectLayer (selectedLayer);
                updateHeaderStatus();
            }
        };
        addAndMakeVisible (factoryPreset);

        userPresets.setTitle ("User settings presets");
        userPresets.setDescription ("Recall layer types and controls. Imported audio files are not included; save your DAW project to retain source-file references.");
        userPresets.setTooltip (userPresets.getDescription());
        userPresets.setTextWhenNothingSelected ("Settings presets");
        userPresets.onChange = [this]
        {
            const auto name = userPresets.getText();
            if (name.isNotEmpty())
            {
                if (! presetManager.loadUserPreset (name))
                {
                    userPresets.setSelectedId (0, juce::dontSendNotification);
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                        "Could not load settings preset", "The preset is missing, unreadable, invalid, or belongs to another product. Your current settings have not changed.");
                    return;
                }
                selectLayer (selectedLayer);
                updateHeaderStatus();
            }
        };
        addAndMakeVisible (userPresets);

        savePresetButton.setTitle ("Save stack settings preset");
        savePresetButton.setDescription (
            "Save layer types, placement and controls only. Imported audio is not included. Save your DAW project to retain source-file references, and keep those files available.");
        savePresetButton.setTooltip (savePresetButton.getDescription());
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
                    "Could not save settings preset", "Check that your preset folder is writable and that the disk has free space. Your current settings remain active.");
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

    void setupLayerSlider (juce::Slider& slider, juce::Label& label,
                           const juce::String& name,
                           const juce::String& suffix = {})
    {
        studio::styleFieldLabel (
            label, name, juce::Justification::centred);
        addAndMakeVisible (label);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
        slider.setTextValueSuffix (suffix);
        slider.setTitle (name);
        slider.setDescription ("Adjust layer " + name.toLowerCase());
        addAndMakeVisible (slider);
    }

    void selectLayer (int layer)
    {
        selectedLayer = juce::jlimit (
            0, rack::SattariInstrumentRack::kNumLayers - 1, layer);
        const int selection = (int) audioProcessor.parameters()
                                  .getRawParameterValue (
                                      rack::SattariInstrumentRack::layerParamId (
                                          selectedLayer))->load();
        auto* module = audioProcessor.moduleAtForLayer (
            selectedLayer, selection - 1);
        inspector.showModule (
            module != nullptr ? module->moduleId() : nullptr,
            module != nullptr ? module->displayName() : nullptr,
            true);
        updateSourceActions();
        resized();
    }

    void updateHeaderStatus()
    {
        int active = 0;
        for (const auto& row : layers)
            if (row.chooser.getSelectedId() > 1
                && ! row.mute.getToggleState())
                ++active;
        header.setStatus (
            active > 0
                ? juce::String (active) + (active == 1 ? " LAYER ACTIVE"
                                                       : " LAYERS ACTIVE")
                : "EMPTY STACK",
            active > 0);
        repaint();
    }

    void updateSourceActions()
    {
        const int count = audioProcessor.sourceSlotCountForLayer (selectedLayer);
        sourceA.setVisible (count >= 1);
        sourceB.setVisible (count >= 2);
        if (count == 0)
        {
            sourceStatus.setText (
                "The selected instrument generates its own sound.",
                juce::dontSendNotification);
            return;
        }

        const auto sourceNameA =
            audioProcessor.sourceNameForLayer (selectedLayer, 0);
        const auto sourceNameB =
            audioProcessor.sourceNameForLayer (selectedLayer, 1);
        juce::String status = "A / "
            + juce::String (audioProcessor.sourceUnavailableForLayer (selectedLayer, 0) ? "Missing or unreadable: " : "")
            + (sourceNameA.isNotEmpty() ? sourceNameA : "not loaded");
        if (count >= 2)
            status += "     B / "
                + juce::String (audioProcessor.sourceUnavailableForLayer (selectedLayer, 1) ? "Missing or unreadable: " : "")
                + (sourceNameB.isNotEmpty() ? sourceNameB : "not loaded");
        sourceStatus.setText (status, juce::dontSendNotification);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        updateSourceActions();
    }

    void chooseSource (int sourceSlot)
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Load audio source", juce::File(),
            "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectFiles,
            [this, sourceSlot] (const juce::FileChooser& chooser)
            {
                const auto file = chooser.getResult();
                if (! file.existsAsFile()) return;
                if (! audioProcessor.loadSourceForLayer (
                        selectedLayer, sourceSlot, file))
                    sourceStatus.setText (
                        "Could not decode " + file.getFileName(),
                        juce::dontSendNotification);
                else
                    updateSourceActions();
            });
    }

    rack::SattariInstrumentRack& audioProcessor;
    const juce::Colour accent;
    studio::LookAndFeel lookAndFeel;
    studio::Header header;
    sattari::presets::SattariPresetManager presetManager;
    SattariABRow abRow;
    SattariModuleInspector inspector;
    juce::Viewport inspectorViewport;

    juce::ComboBox factoryPreset, userPresets;
    juce::TextButton savePresetButton { "Save" };
    juce::TextButton sourceA, sourceB;
    juce::Label sourceStatus;
    std::unique_ptr<juce::FileChooser> fileChooser;

    struct LayerRow
    {
        juce::Rectangle<int> bounds;
        juce::Label number;
        juce::ComboBox chooser;
        juce::ToggleButton mute { "Mute" };
        juce::Slider level, pan, low, high, transpose;
        juce::Label levelLabel, panLabel, lowLabel, highLabel, transposeLabel;
        std::unique_ptr<ComboAttachment> chooserAtt;
        std::unique_ptr<ButtonAttachment> muteAtt;
        std::unique_ptr<SliderAttachment> levelAtt, panAtt, lowAtt,
                                          highAtt, transposeAtt;
    };
    std::array<LayerRow, rack::SattariInstrumentRack::kNumLayers> layers;

    juce::Rectangle<int> layerArea, inspectorArea;
    int selectedLayer = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariStackEditor)
};

} // namespace sattari::ui
