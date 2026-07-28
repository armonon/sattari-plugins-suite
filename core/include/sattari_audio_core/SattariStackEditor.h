#pragma once

#include <sattari_audio_core/SattariInstrumentRack.h>
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
          lookAndFeel (sattari::brand::productAccent (brandSlug))
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
            row.number.setColour (juce::Label::textColourId, juce::Colour (br::kTextMuted));
            addAndMakeVisible (row.number);

            row.chooser.addItemList (menu, 1);
            addAndMakeVisible (row.chooser);
            row.chooserAtt = std::make_unique<CA> (processor.parameters(), IR::layerParamId (L), row.chooser);

            row.mute.setColour (juce::ToggleButton::textColourId, juce::Colour (br::kTextMuted));
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
        }

        setSize (620, 340);
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
        title.setBounds (top.removeFromLeft (80));
        subtitle.setBounds (top.withTrimmedTop (7));
        r.removeFromTop (8);

        for (auto& row : layers)
        {
            auto line = r.removeFromTop (30);
            row.number.setBounds (line.removeFromLeft (18));
            row.chooser.setBounds (line.removeFromLeft (120));
            line.removeFromLeft (6);
            row.mute.setBounds (line.removeFromLeft (58));
            row.level.setBounds (line.removeFromLeft (110));
            row.pan.setBounds   (line.removeFromLeft (100));
            row.lo.setBounds    (line.removeFromLeft (80));
            row.hi.setBounds    (line.removeFromLeft (80));
            row.trans.setBounds (line);
            r.removeFromTop (8);
        }
    }

private:
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;

    rack::SattariInstrumentRack& processor;
    SattariLookAndFeel lookAndFeel;

    juce::Label title, subtitle;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariStackEditor)
};

} // namespace sattari::ui
