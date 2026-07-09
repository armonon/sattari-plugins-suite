#include "PluginEditor.h"

SattariWarbleAudioProcessorEditor::SattariWarbleAudioProcessorEditor (SattariWarbleAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (700, 320);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Warble");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Tape / vinyl lo-fi character");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    // The Age macro — the hero control.
    ageKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    ageKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
    addAndMakeVisible (ageKnob);
    ageAtt = std::make_unique<SA> (processor.parameters(), "age", ageKnob);
    ageCap.setText ("AGE", juce::dontSendNotification);
    ageCap.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    ageCap.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    ageCap.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (ageCap);

    addKnob ("wow",     "WOW");
    addKnob ("flutter", "FLUTTER");
    addKnob ("sat",     "SATURATE");
    addKnob ("tone",    "HEAD BUMP");
    addKnob ("hiss",    "HISS");
    addKnob ("crackle", "CRACKLE");
    addKnob ("mix",     "MIX");
    addKnob ("output",  "OUTPUT", " dB");
}

SattariWarbleAudioProcessorEditor::~SattariWarbleAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SattariWarbleAudioProcessorEditor::addKnob (const juce::String& id, const juce::String& cap, const juce::String& suffix)
{
    auto* s = new juce::Slider();
    s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 15);
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

void SattariWarbleAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariWarbleAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (16);
    title.setBounds (b.removeFromTop (34));
    subtitle.setBounds (b.removeFromTop (20));
    b.removeFromTop (12);

    // Left column: big Age macro. Right: the 8 trim knobs in a 4x2 grid.
    auto left = b.removeFromLeft (160);
    ageCap.setBounds (left.removeFromBottom (16));
    ageKnob.setBounds (left.reduced (8, 4));

    b.removeFromLeft (12);

    auto place = [] (juce::Slider* s, juce::Label* c, juce::Rectangle<int> cell)
    { c->setBounds (cell.removeFromBottom (15)); s->setBounds (cell.reduced (4, 2)); };

    const int rowH = b.getHeight() / 2;
    auto row1 = b.removeFromTop (rowH);
    auto row2 = b;
    const int w = row1.getWidth() / 4;

    for (int i = 0; i < 4; ++i)
        place (knobs[i], caps[i], row1.removeFromLeft (w));
    for (int i = 0; i < 4; ++i)
        place (knobs[i + 4], caps[i + 4], row2.removeFromLeft (w));
}
