#include "PluginEditor.h"

namespace br = sattari::brand;

SattariMixAudioProcessorEditor::SattariMixAudioProcessorEditor (SattariMixAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    title.setText ("Mix", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, juce::Colour (br::kTextPrimary));
    addAndMakeVisible (title);
    subtitle.setText (juce::String::fromUTF8 ("module rack \xc2\xb7 processes top to bottom"),
                      juce::dontSendNotification);
    subtitle.setColour (juce::Label::textColourId, juce::Colour (br::kTextMuted));
    addAndMakeVisible (subtitle);

    const auto menu = processor.moduleMenuNames();
    for (int s = 0; s < SattariMixAudioProcessor::kNumSlots; ++s)
    {
        auto& row = slots[(size_t) s];
        row.number.setText (juce::String (s + 1), juce::dontSendNotification);
        row.number.setColour (juce::Label::textColourId, juce::Colour (br::kTextMuted));
        addAndMakeVisible (row.number);

        row.chooser.addItemList (menu, 1);
        addAndMakeVisible (row.chooser);
        row.chooserAtt = std::make_unique<CA> (processor.parameters(),
                                               "slot" + juce::String (s + 1), row.chooser);

        row.bypass.setColour (juce::ToggleButton::textColourId, juce::Colour (br::kTextMuted));
        addAndMakeVisible (row.bypass);
        row.bypassAtt = std::make_unique<BA> (processor.parameters(),
                                              "slot" + juce::String (s + 1) + "bypass", row.bypass);
    }

    setSize (460, 400);
}

SattariMixAudioProcessorEditor::~SattariMixAudioProcessorEditor() { setLookAndFeel (nullptr); }

void SattariMixAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (br::kPanel));
    g.setColour (juce::Colour (br::kPanelDeep));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (6.0f), 10.0f);
}

void SattariMixAudioProcessorEditor::resized()
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
