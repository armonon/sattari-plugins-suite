#pragma once

#include <sattari_audio_core/SattariInstrumentRack.h>
#include <sattari_audio_core/SattariModuleInspector.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>

#include <array>
#include <memory>
#include <string>

// =============================================================================
//  SattariStackEditor — the shared INSTRUMENT-rack UI.
// -----------------------------------------------------------------------------
//  One row per LAYER, and the row is wider than an effect rack's because a layer
//  is a placement, not just a choice: instrument, mute, level, pan, key range and
//  transpose all belong on the same line, since they are what turn a stack into a
//  split or an octave layer rather than a unison pile.
// =============================================================================

namespace sattari::ui
{

class SattariStackEditor : public juce::AudioProcessorEditor
{
public:
    SattariStackEditor (rack::SattariInstrumentRack& p,
                        const juce::String& titleText,
                        const juce::String& subtitleText,
                        const std::string& brandSlug)
        : juce::AudioProcessorEditor (&p),
          processor (p),
          lookAndFeel (sattari::brand::productAccent (brandSlug)),
          inspector (p.parameters())
    {
        namespace br = sattari::brand;
        setLookAndFeel (&lookAndFeel);

        title.setText (titleText, juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, juce::Colour (br::kTextPrimary));
        addAndMakeVisible (title);

        subtitle.setText (subtitleText, juce::dontSendNotification);
        subtitle.setColour (juce::Label::textColourId, juce::Colour (br::kTextMuted));
        addAndMakeVisible (subtitle);

        const auto menu = processor.moduleMenuNames();
        using IR = rack::SattariInstrumentRack;

        for (int L = 0; L < IR::kNumLayers; ++L)
        {
            auto& row = layers[(size_t) L];

            row.number.setText (juce::String (L + 1), juce::dontSendNotification);
            row.number.setTitle ("Layer " + juce::String (L + 1));
            row.number.setColour (juce::Label::textColourId, juce::Colour (br::kTextMuted));
            addAndMakeVisible (row.number);

            row.chooser.addItemList (menu, 1);
            row.chooser.setTitle ("Layer " + juce::String (L + 1) + " instrument");
            row.chooser.setDescription ("Choose the instrument for this layer");
            addAndMakeVisible (row.chooser);
            row.chooserAtt = std::make_unique<CA> (processor.parameters(), IR::layerParamId (L), row.chooser);

            row.mute.setColour (juce::ToggleButton::textColourId, juce::Colour (br::kTextMuted));
            row.mute.setTitle ("Layer " + juce::String (L + 1) + " mute");
            addAndMakeVisible (row.mute);
            row.muteAtt = std::make_unique<BA> (processor.parameters(), IR::layerMuteParamId (L), row.mute);

            auto slider = [this] (juce::Slider& s)
            {
                s.setSliderStyle (juce::Slider::LinearHorizontal);
                s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
                s.setColour (juce::Slider::textBoxTextColourId, juce::Colour (sattari::brand::kTextMuted));
                addAndMakeVisible (s);
            };
            slider (row.level);  row.levelAtt = std::make_unique<SA> (processor.parameters(), IR::layerLevelParamId (L), row.level);
            slider (row.pan);    row.panAtt   = std::make_unique<SA> (processor.parameters(), IR::layerPanParamId (L),   row.pan);
            slider (row.lo);     row.loAtt    = std::make_unique<SA> (processor.parameters(), IR::layerLoParamId (L),    row.lo);
            slider (row.hi);     row.hiAtt    = std::make_unique<SA> (processor.parameters(), IR::layerHiParamId (L),    row.hi);
            slider (row.trans);  row.transAtt = std::make_unique<SA> (processor.parameters(), IR::layerTransParamId (L), row.trans);

            row.chooser.onChange = [this, L] { selectLayer (L); };
        }

        sourceA.setButtonText ("Load source A");
        sourceA.setTitle ("Load source A");
        sourceA.setDescription ("Load audio into the selected Chop or Granulator layer");
        sourceA.onClick = [this] { chooseSource (0); };
        addAndMakeVisible (sourceA);

        sourceB.setButtonText ("Load source B");
        sourceB.setTitle ("Load source B");
        sourceB.setDescription ("Load audio into Granulator source B");
        sourceB.onClick = [this] { chooseSource (1); };
        addAndMakeVisible (sourceB);

        sourceStatus.setColour (juce::Label::textColourId,
                                juce::Colour (br::kTextMuted));
        sourceStatus.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (sourceStatus);

        inspectorViewport.setViewedComponent (&inspector, false);
        inspectorViewport.setScrollBarsShown (true, false);
        inspectorViewport.setTitle ("Selected instrument controls");
        addAndMakeVisible (inspectorViewport);

        int firstSelected = 0;
        for (int layer = 0; layer < IR::kNumLayers; ++layer)
            if (processor.parameters().getRawParameterValue (
                    IR::layerParamId (layer))->load() > 0.0f)
            {
                firstSelected = layer;
                break;
            }
        selectLayer (firstSelected);

        setResizable (true, true);
        setResizeLimits (860, 460, 1500, 950);
        setSize (1080, 540);
    }

    ~SattariStackEditor() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        namespace br = sattari::brand;
        g.fillAll (juce::Colour (br::kPanel));
        g.setColour (juce::Colour (br::kPanelDeep));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (6.0f), 10.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        auto top = r.removeFromTop (30);
        title.setBounds (top.removeFromLeft (juce::jmin (180, top.getWidth() / 3)));
        subtitle.setBounds (top.withTrimmedTop (7));
        r.removeFromTop (8);

        auto layerPanel = r.removeFromLeft (juce::jlimit (590, 690, getWidth() * 2 / 3));
        r.removeFromLeft (12);
        inspectorViewport.setBounds (r);

        for (auto& row : layers)
        {
            auto line = layerPanel.removeFromTop (30);
            row.number.setBounds (line.removeFromLeft (18));
            row.chooser.setBounds (line.removeFromLeft (112));
            line.removeFromLeft (6);
            row.mute.setBounds (line.removeFromLeft (56));
            row.level.setBounds (line.removeFromLeft (106));
            row.pan.setBounds   (line.removeFromLeft (96));
            row.lo.setBounds    (line.removeFromLeft (76));
            row.hi.setBounds    (line.removeFromLeft (76));
            row.trans.setBounds (line);
            layerPanel.removeFromTop (8);
        }

        layerPanel.removeFromTop (6);
        auto sourceLine = layerPanel.removeFromTop (30);
        sourceA.setBounds (sourceLine.removeFromLeft (130));
        sourceLine.removeFromLeft (6);
        sourceB.setBounds (sourceLine.removeFromLeft (130));
        layerPanel.removeFromTop (4);
        sourceStatus.setBounds (layerPanel.removeFromTop (46));
    }

private:
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;

    rack::SattariInstrumentRack& processor;
    SattariLookAndFeel lookAndFeel;
    SattariModuleInspector inspector;
    juce::Viewport inspectorViewport;

    juce::Label title, subtitle, sourceStatus;
    juce::TextButton sourceA, sourceB;
    std::unique_ptr<juce::FileChooser> fileChooser;
    struct LayerRow
    {
        juce::Label        number;
        juce::ComboBox     chooser;
        juce::ToggleButton mute { "Mute" };
        juce::Slider       level, pan, lo, hi, trans;
        std::unique_ptr<CA> chooserAtt;
        std::unique_ptr<BA> muteAtt;
        std::unique_ptr<SA> levelAtt, panAtt, loAtt, hiAtt, transAtt;
    };
    std::array<LayerRow, rack::SattariInstrumentRack::kNumLayers> layers;

    void selectLayer (int layer)
    {
        selectedLayer = juce::jlimit (
            0, rack::SattariInstrumentRack::kNumLayers - 1, layer);
        const int selection = (int) processor.parameters().getRawParameterValue (
            rack::SattariInstrumentRack::layerParamId (selectedLayer))->load();
        auto* module = processor.moduleAtForLayer (selectedLayer, selection - 1);
        inspector.showModule (module != nullptr ? module->moduleId() : nullptr,
                              module != nullptr ? module->displayName() : nullptr,
                              true);
        updateSourceActions();
        resized();
    }

    void updateSourceActions()
    {
        const int count = processor.sourceSlotCountForLayer (selectedLayer);
        sourceA.setVisible (count >= 1);
        sourceB.setVisible (count >= 2);
        if (count == 0)
        {
            sourceStatus.setText (
                "This instrument has no external audio source.",
                juce::dontSendNotification);
            return;
        }

        const auto a = processor.sourceNameForLayer (selectedLayer, 0);
        const auto b = processor.sourceNameForLayer (selectedLayer, 1);
        juce::String status = "A: " + (a.isNotEmpty() ? a : "not loaded");
        if (count >= 2)
            status += "    B: " + (b.isNotEmpty() ? b : "not loaded");
        sourceStatus.setText (status, juce::dontSendNotification);
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
                if (file.existsAsFile())
                {
                    const bool ok = processor.loadSourceForLayer (
                        selectedLayer, sourceSlot, file);
                    if (! ok)
                        sourceStatus.setText (
                            "Could not decode " + file.getFileName(),
                            juce::dontSendNotification);
                    else
                        updateSourceActions();
                }
            });
    }

    int selectedLayer = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariStackEditor)
};

} // namespace sattari::ui
