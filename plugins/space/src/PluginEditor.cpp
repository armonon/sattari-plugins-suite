#include "PluginEditor.h"

SattariSpaceAudioProcessorEditor::SattariSpaceAudioProcessorEditor (SattariSpaceAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Space");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Reverb + tempo-synced delay");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    tempoLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    tempoLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    tempoLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (tempoLabel);

    addKnob ("size", "SIZE");
    addKnob ("damp", "DAMP");
    addKnob ("revmix", "REV MIX");

    div.addItemList (SattariSpaceAudioProcessor::divisionChoices(), 1);
    addAndMakeVisible (div);
    divAtt = std::make_unique<CA> (processor.parameters(), "div", div);
    divCap.setText ("DELAY", juce::dontSendNotification);
    divCap.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    divCap.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    divCap.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (divCap);

    addKnob ("feedback", "FEEDBACK");
    addKnob ("delaymix", "DLY MIX");
    addKnob ("width", "WIDTH");
    addKnob ("output", "OUTPUT", " dB");

    // setSize() synchronously calls resized(). Delay it until every knob/caption
    // exists; the old constructor indexed empty OwnedArrays during hosted editor
    // creation, which made Space crash inside its own resized() callback.
    setSize (700, 300);
    startTimerHz (8);
}

SattariSpaceAudioProcessorEditor::~SattariSpaceAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariSpaceAudioProcessorEditor::addKnob (const juce::String& id, const juce::String& cap, const juce::String& suffix)
{
    auto* s = new juce::Slider();
    s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 15);
    s->setTextValueSuffix (suffix);
    addAndMakeVisible (s); knobs.add (s);
    auto* c = new juce::Label();
    c->setText (cap, juce::dontSendNotification);
    c->setFont (juce::FontOptions (9.5f, juce::Font::bold));
    c->setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    c->setJustificationType (juce::Justification::centred);
    addAndMakeVisible (c); caps.add (c);
    atts.push_back (std::make_unique<SA> (processor.parameters(), id, *s));
}

void SattariSpaceAudioProcessorEditor::timerCallback()
{
    tempoLabel.setText (juce::String (processor.currentBpm(), 1) + " BPM", juce::dontSendNotification);
}

void SattariSpaceAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariSpaceAudioProcessorEditor::resized()
{
    if (knobs.size() < 7 || caps.size() < 7)
        return;
    auto b = getLocalBounds().reduced (16);
    auto top = b.removeFromTop (34);
    title.setBounds (top.removeFromLeft (400));
    tempoLabel.setBounds (top);
    subtitle.setBounds (b.removeFromTop (20));
    b.removeFromTop (14);

    // 4 reverb-ish knobs top row, delay controls bottom row.
    auto row1 = b.removeFromTop (110);
    const int w = b.getWidth() / 4;
    auto place = [] (juce::Slider* s, juce::Label* c, juce::Rectangle<int> cell)
    { c->setBounds (cell.removeFromBottom (15)); s->setBounds (cell.reduced (4, 2)); };

    place (knobs[0], caps[0], row1.removeFromLeft (w));   // size
    place (knobs[1], caps[1], row1.removeFromLeft (w));   // damp
    place (knobs[2], caps[2], row1.removeFromLeft (w));   // revmix
    place (knobs[5], caps[5], row1.removeFromLeft (w));   // width

    b.removeFromTop (8);
    auto row2 = b.removeFromTop (110);
    auto dcell = row2.removeFromLeft (w);
    divCap.setBounds (dcell.removeFromBottom (15));
    div.setBounds (dcell.withSizeKeepingCentre (dcell.getWidth() - 8, 28));
    place (knobs[3], caps[3], row2.removeFromLeft (w));   // feedback
    place (knobs[4], caps[4], row2.removeFromLeft (w));   // delaymix
    place (knobs[6], caps[6], row2.removeFromLeft (w));   // output
}
