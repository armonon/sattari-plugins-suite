#include "PluginEditor.h"

SattariPulseAudioProcessorEditor::SattariPulseAudioProcessorEditor (SattariPulseAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (700, 300);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Pulse");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Tempo-locked pump / gate");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    tempoLabel.setFont (sattari::brand::fontOptions (11.0f, juce::Font::bold));
    tempoLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    tempoLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (tempoLabel);

    addCombo (shape, "shape", SattariPulseAudioProcessor::shapeChoices(), shapeCap, "SHAPE");
    shapeAtt = std::make_unique<CA> (processor.parameters(), "shape", shape);

    addCombo (rate, "rate", SattariPulseAudioProcessor::rateChoices(), rateCap, "RATE");
    rateAtt = std::make_unique<CA> (processor.parameters(), "rate", rate);

    addKnob ("depth",  "DEPTH");
    addKnob ("smooth", "SMOOTH");
    addKnob ("filter", "FILTER");
    addKnob ("mix",    "MIX");

    startTimerHz (30);
}

SattariPulseAudioProcessorEditor::~SattariPulseAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariPulseAudioProcessorEditor::addCombo (juce::ComboBox& box, const juce::String& id,
                                                 const juce::StringArray& items,
                                                 juce::Label& cap, const juce::String& capText)
{
    juce::ignoreUnused (id);
    box.addItemList (items, 1);
    addAndMakeVisible (box);
    cap.setText (capText, juce::dontSendNotification);
    cap.setFont (sattari::brand::fontOptions (9.5f, juce::Font::bold));
    cap.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    cap.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (cap);
}

void SattariPulseAudioProcessorEditor::addKnob (const juce::String& id, const juce::String& cap, const juce::String& suffix)
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

void SattariPulseAudioProcessorEditor::timerCallback()
{
    tempoLabel.setText (juce::String (processor.currentBpm(), 1) + " BPM", juce::dontSendNotification);
    const float g = processor.currentGain();
    if (std::abs (g - meterGain) > 1.0e-3f)
    {
        meterGain = g;
        repaint (meterBounds);
    }
}

void SattariPulseAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    // Pump indicator: a bar whose fill drops on the beat (ducking) and refills.
    if (! meterBounds.isEmpty())
    {
        auto r = meterBounds.toFloat();
        g.setColour (juce::Colour (sattari::brand::kTextMuted).withAlpha (0.25f));
        g.fillRoundedRectangle (r, 3.0f);
        const float filled = juce::jlimit (0.0f, 1.0f, meterGain);
        auto fill = r.withWidth (r.getWidth() * filled);
        g.setColour (sattari::brand::accentFor ("pulse").withAlpha (0.85f));
        g.fillRoundedRectangle (fill, 3.0f);
    }
}

void SattariPulseAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (16);
    auto top = b.removeFromTop (34);
    title.setBounds (top.removeFromLeft (400));
    tempoLabel.setBounds (top);
    subtitle.setBounds (b.removeFromTop (20));
    b.removeFromTop (10);

    // Pump indicator bar under the subtitle.
    meterBounds = b.removeFromTop (10);
    b.removeFromTop (10);

    // Row 1: shape + rate selectors, then depth + smooth knobs.
    auto row1 = b.removeFromTop (110);
    const int w = b.getWidth() / 4;
    auto place = [] (juce::Slider* s, juce::Label* c, juce::Rectangle<int> cell)
    { c->setBounds (cell.removeFromBottom (15)); s->setBounds (cell.reduced (4, 2)); };

    auto scell = row1.removeFromLeft (w);
    shapeCap.setBounds (scell.removeFromBottom (15));
    shape.setBounds (scell.withSizeKeepingCentre (scell.getWidth() - 8, 28));

    auto rcell = row1.removeFromLeft (w);
    rateCap.setBounds (rcell.removeFromBottom (15));
    rate.setBounds (rcell.withSizeKeepingCentre (rcell.getWidth() - 8, 28));

    place (knobs[0], caps[0], row1.removeFromLeft (w));   // depth
    place (knobs[1], caps[1], row1.removeFromLeft (w));   // smooth

    b.removeFromTop (8);
    auto row2 = b.removeFromTop (110);
    place (knobs[2], caps[2], row2.removeFromLeft (w));   // filter
    place (knobs[3], caps[3], row2.removeFromLeft (w));   // mix
}
