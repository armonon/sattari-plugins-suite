#include "PluginEditor.h"

SattariEchoAudioProcessorEditor::SattariEchoAudioProcessorEditor (SattariEchoAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (700, 320);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Echo");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Tempo-locked ping-pong delay");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    tempoLabel.setFont (sattari::brand::fontOptions (11.0f, juce::Font::bold));
    tempoLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    tempoLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (tempoLabel);

    // Time controls: SYNC toggle + division combo + free ms knob.
    addAndMakeVisible (syncBtn);
    syncAtt = std::make_unique<BA> (processor.parameters(), "sync", syncBtn);

    div.addItemList (SattariEchoAudioProcessor::divisionChoices(), 1);
    addAndMakeVisible (div);
    divAtt = std::make_unique<CA> (processor.parameters(), "div", div);
    divCap.setText ("DIVISION", juce::dontSendNotification);
    divCap.setFont (sattari::brand::fontOptions (9.5f, juce::Font::bold));
    divCap.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    divCap.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (divCap);

    addKnob ("timems", "FREE", " ms");

    addAndMakeVisible (pingBtn);
    pingAtt = std::make_unique<BA> (processor.parameters(), "pingpong", pingBtn);

    addKnob ("feedback", "FEEDBACK");
    addKnob ("tone", "TONE");
    addKnob ("diffusion", "DIFFUSE");
    addKnob ("width", "WIDTH");
    addKnob ("duck", "DUCK");
    addKnob ("mix", "MIX");
    addKnob ("output", "OUTPUT", " dB");

    startTimerHz (8);
}

SattariEchoAudioProcessorEditor::~SattariEchoAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariEchoAudioProcessorEditor::addKnob (const juce::String& id, const juce::String& cap, const juce::String& suffix)
{
    auto* s = new juce::Slider();
    s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 15);
    s->setTextValueSuffix (suffix);
    addAndMakeVisible (s); knobs.add (s);
    auto* c = new juce::Label();
    c->setText (cap, juce::dontSendNotification);
    c->setFont (sattari::brand::fontOptions (9.5f, juce::Font::bold));
    c->setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    c->setJustificationType (juce::Justification::centred);
    addAndMakeVisible (c); caps.add (c);
    atts.push_back (std::make_unique<SA> (processor.parameters(), id, *s));
}

void SattariEchoAudioProcessorEditor::timerCallback()
{
    tempoLabel.setText (juce::String (processor.currentBpm(), 1) + " BPM", juce::dontSendNotification);
}

void SattariEchoAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariEchoAudioProcessorEditor::resized()
{
    // knobs index map (addKnob order):
    //   0 timems, 1 feedback, 2 tone, 3 diffusion, 4 width, 5 duck, 6 mix, 7 output
    auto b = getLocalBounds().reduced (16);
    auto top = b.removeFromTop (34);
    title.setBounds (top.removeFromLeft (400));
    tempoLabel.setBounds (top);
    subtitle.setBounds (b.removeFromTop (20));
    b.removeFromTop (12);

    const int w = b.getWidth() / 4;
    auto place = [] (juce::Slider* s, juce::Label* c, juce::Rectangle<int> cell)
    { c->setBounds (cell.removeFromBottom (15)); s->setBounds (cell.reduced (4, 2)); };

    // Row 1: time section (sync toggle + division combo + free knob) then feedback.
    auto row1 = b.removeFromTop (112);
    auto tcell = row1.removeFromLeft (w);
    {
        auto tt = tcell;
        syncBtn.setBounds (tt.removeFromTop (24).reduced (6, 0));
        divCap.setBounds (tt.removeFromBottom (15));
        div.setBounds (tt.withSizeKeepingCentre (tt.getWidth() - 8, 28));
    }
    place (knobs[0], caps[0], row1.removeFromLeft (w));   // free ms
    place (knobs[1], caps[1], row1.removeFromLeft (w));   // feedback
    // ping-pong toggle sits in the 4th cell of row 1
    pingBtn.setBounds (row1.removeFromLeft (w).withSizeKeepingCentre (w - 12, 24));

    b.removeFromTop (8);
    auto row2 = b.removeFromTop (112);
    place (knobs[2], caps[2], row2.removeFromLeft (w));   // tone
    place (knobs[3], caps[3], row2.removeFromLeft (w));   // diffusion
    place (knobs[4], caps[4], row2.removeFromLeft (w));   // width
    place (knobs[5], caps[5], row2.removeFromLeft (w));   // duck

    b.removeFromTop (8);
    auto row3 = b.removeFromTop (60);
    place (knobs[6], caps[6], row3.removeFromLeft (w));   // mix
    place (knobs[7], caps[7], row3.removeFromLeft (w));   // output
}
