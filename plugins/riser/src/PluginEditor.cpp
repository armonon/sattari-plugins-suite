#include "PluginEditor.h"

SattariRiserAudioProcessorEditor::SattariRiserAudioProcessorEditor (SattariRiserAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (720, 320);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Riser");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Tempo-synced riser / downlifter / impact");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    tempoLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    tempoLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    tempoLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (tempoLabel);

    // Mode + Length combos.
    modeBox.addItemList (SattariRiserAudioProcessor::modeChoices(), 1);
    addAndMakeVisible (modeBox);
    modeAtt = std::make_unique<CA> (processor.parameters(), "mode", modeBox);
    modeCap.setText ("MODE", juce::dontSendNotification);
    modeCap.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    modeCap.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    modeCap.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (modeCap);

    lengthBox.addItemList (SattariRiserAudioProcessor::lengthChoices(), 1);
    addAndMakeVisible (lengthBox);
    lengthAtt = std::make_unique<CA> (processor.parameters(), "length", lengthBox);
    lengthCap.setText ("LENGTH", juce::dontSendNotification);
    lengthCap.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    lengthCap.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    lengthCap.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (lengthCap);

    // Arm + Host-trigger toggles.
    addAndMakeVisible (armButton);
    armAtt = std::make_unique<BA> (processor.parameters(), "arm", armButton);
    addAndMakeVisible (hostTrigButton);
    hostTrigAtt = std::make_unique<BA> (processor.parameters(), "hostTrigger", hostTrigButton);

    // Knobs.
    addKnob ("noiseTone", "NOISE/TONE");
    addKnob ("sweepLo",   "SWEEP LO", " Hz");
    addKnob ("sweepHi",   "SWEEP HI", " Hz");
    addKnob ("reso",      "RESO");
    addKnob ("pitchRise", "PITCH", " st");
    addKnob ("stutter",   "STUTTER");
    addKnob ("mix",       "MIX");
    addKnob ("output",    "OUTPUT", " dB");

    startTimerHz (30);
}

SattariRiserAudioProcessorEditor::~SattariRiserAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariRiserAudioProcessorEditor::addKnob (const juce::String& id, const juce::String& cap, const juce::String& suffix)
{
    auto* s = new juce::Slider();
    s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 15);
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

void SattariRiserAudioProcessorEditor::timerCallback()
{
    tempoLabel.setText (juce::String (processor.currentBpm(), 1) + " BPM", juce::dontSendNotification);
    repaint (meterBounds.toNearestInt());   // animate the progress bar while building
}

void SattariRiserAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    // Build-progress meter.
    const auto accent = juce::Colour (sattari::brand::productAccent ("riser"));
    g.setColour (juce::Colour (sattari::brand::kPanelDeep));
    g.fillRoundedRectangle (meterBounds, 4.0f);
    g.setColour (juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (meterBounds, 4.0f, 1.0f);

    const float prog = juce::jlimit (0.0f, 1.0f, processor.buildProgress());
    if (processor.isBuilding() && prog > 0.0f)
    {
        auto fill = meterBounds.reduced (2.0f);
        fill = fill.withWidth (fill.getWidth() * prog);
        g.setColour (accent.withAlpha (0.85f));
        g.fillRoundedRectangle (fill, 3.0f);
    }

    g.setColour (juce::Colour (processor.isBuilding() ? sattari::brand::kTextPrimary
                                                      : sattari::brand::kTextFaint));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (processor.isBuilding() ? "BUILDING" : "IDLE",
                meterBounds.toNearestInt(), juce::Justification::centred, false);
}

void SattariRiserAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (16);
    auto top = b.removeFromTop (34);
    title.setBounds (top.removeFromLeft (420));
    tempoLabel.setBounds (top);
    subtitle.setBounds (b.removeFromTop (20));
    b.removeFromTop (8);

    // Control row: mode, length, arm, host-trigger, then the progress meter.
    auto ctl = b.removeFromTop (54);
    auto modeCell = ctl.removeFromLeft (120);
    modeCap.setBounds (modeCell.removeFromTop (14));
    modeBox.setBounds (modeCell.withSizeKeepingCentre (modeCell.getWidth() - 6, 28));
    ctl.removeFromLeft (8);
    auto lenCell = ctl.removeFromLeft (110);
    lengthCap.setBounds (lenCell.removeFromTop (14));
    lengthBox.setBounds (lenCell.withSizeKeepingCentre (lenCell.getWidth() - 6, 28));
    ctl.removeFromLeft (12);
    armButton.setBounds (ctl.removeFromLeft (90).withSizeKeepingCentre (90, 28));
    hostTrigButton.setBounds (ctl.removeFromLeft (120).withSizeKeepingCentre (120, 28));
    ctl.removeFromLeft (12);
    meterBounds = ctl.withSizeKeepingCentre (ctl.getWidth(), 22).toFloat();

    b.removeFromTop (14);

    // Two rows of four knobs.
    auto place = [] (juce::Slider* s, juce::Label* c, juce::Rectangle<int> cell)
    { c->setBounds (cell.removeFromBottom (15)); s->setBounds (cell.reduced (4, 2)); };

    const int w = b.getWidth() / 4;
    auto row1 = b.removeFromTop (100);
    for (int i = 0; i < 4; ++i)
        place (knobs[i], caps[i], row1.removeFromLeft (w));
    b.removeFromTop (6);
    auto row2 = b.removeFromTop (100);
    for (int i = 4; i < 8; ++i)
        place (knobs[i], caps[i], row2.removeFromLeft (w));
}
