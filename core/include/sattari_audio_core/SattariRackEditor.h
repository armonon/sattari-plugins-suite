#pragma once

#include <sattari_audio_core/SattariRackProcessor.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>

#include <array>
#include <memory>
#include <string>

// =============================================================================
//  SattariRackEditor — the shared rack UI.
// -----------------------------------------------------------------------------
//  One row per slot (module chooser + bypass), so the chain reads top-to-bottom
//  in the order it processes. The slot menu is populated from the processor's
//  roster, so a rack that gains a module gains it in the UI with no edit here.
//
//  Lives beside SattariRackProcessor for the same reason: Mix and Create differ
//  only in title, strapline and accent colour.
// =============================================================================

namespace sattari::ui
{

class SattariRackEditor : public juce::AudioProcessorEditor
{
public:
    SattariRackEditor (rack::SattariRackProcessor& p,
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
        for (int s = 0; s < rack::SattariRackProcessor::kNumSlots; ++s)
        {
            auto& row = slots[(size_t) s];
            row.number.setText (juce::String (s + 1), juce::dontSendNotification);
            row.number.setColour (juce::Label::textColourId, juce::Colour (br::kTextMuted));
            addAndMakeVisible (row.number);

            row.chooser.addItemList (menu, 1);
            addAndMakeVisible (row.chooser);
            row.chooserAtt = std::make_unique<CA> (processor.parameters(),
                                                   rack::SattariRackProcessor::slotParamId (s), row.chooser);

            row.bypass.setColour (juce::ToggleButton::textColourId, juce::Colour (br::kTextMuted));
            addAndMakeVisible (row.bypass);
            row.bypassAtt = std::make_unique<BA> (processor.parameters(),
                                                  rack::SattariRackProcessor::slotBypassParamId (s), row.bypass);
        }

        setSize (460, 400);
    }

    ~SattariRackEditor() override { setLookAndFeel (nullptr); }

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
        title.setBounds (top.removeFromLeft (70));
        subtitle.setBounds (top.withTrimmedTop (7));
        r.removeFromTop (10);

        for (auto& row : slots)
        {
            auto line = r.removeFromTop (32);
            row.number.setBounds (line.removeFromLeft (20));
            row.bypass.setBounds (line.removeFromRight (86));
            line.removeFromRight (8);
            row.chooser.setBounds (line);
            r.removeFromTop (6);
        }
    }

private:
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    rack::SattariRackProcessor& processor;
    SattariLookAndFeel lookAndFeel;

    juce::Label title, subtitle;
    struct SlotRow
    {
        juce::Label    number;
        juce::ComboBox chooser;
        juce::ToggleButton bypass { "Bypass" };
        std::unique_ptr<CA> chooserAtt;
        std::unique_ptr<BA> bypassAtt;
    };
    std::array<SlotRow, rack::SattariRackProcessor::kNumSlots> slots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariRackEditor)
};

} // namespace sattari::ui
