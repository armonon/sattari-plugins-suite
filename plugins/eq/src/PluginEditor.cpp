#include "PluginEditor.h"

SattariEQAudioProcessorEditor::SattariEQAudioProcessorEditor (SattariEQAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (720, 360);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari EQ");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Dynamic parametric EQ");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    addKnob ("hpf", "HPF", " Hz");
    for (int i = 1; i <= SattariEQAudioProcessor::kBands; ++i)
    {
        const juce::String b (i);
        addKnob ("f" + b, "F" + b, " Hz");
        addKnob ("g" + b, "GAIN" + b, " dB");
        addKnob ("q" + b, "Q" + b);
        addKnob ("d" + b, "DYN" + b);
    }
    addKnob ("lpf", "LPF", " Hz");
    addKnob ("output", "OUT", " dB");
}

SattariEQAudioProcessorEditor::~SattariEQAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

juce::Slider& SattariEQAudioProcessorEditor::addKnob (const juce::String& id, const juce::String& caption,
                                                      const juce::String& suffix)
{
    auto* s = new juce::Slider();
    s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 15);
    s->setTextValueSuffix (suffix);
    addAndMakeVisible (s);
    knobs.add (s);

    auto* c = new juce::Label();
    c->setText (caption, juce::dontSendNotification);
    c->setFont (sattari::brand::fontOptions (9.5f, juce::Font::bold));
    c->setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    c->setJustificationType (juce::Justification::centred);
    addAndMakeVisible (c);
    caps.add (c);

    atts.push_back (std::make_unique<SA> (processor.parameters(), id, *s));
    return *s;
}

void SattariEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariEQAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (16);
    title.setBounds (b.removeFromTop (32));
    subtitle.setBounds (b.removeFromTop (20));
    b.removeFromTop (10);

    // Row 1: HPF + the 3 bands' gains prominently; row 2: freq/Q/dyn + LPF/out.
    // Simple flow layout: 8 columns per row.
    const int cols = 8;
    const int cw = b.getWidth() / cols;
    const int rh = 116;
    for (int i = 0; i < knobs.size(); ++i)
    {
        const int r = i / cols, c = i % cols;
        auto cell = juce::Rectangle<int> (b.getX() + c * cw, b.getY() + r * rh, cw, rh);
        caps[i]->setBounds (cell.removeFromBottom (15));
        knobs[i]->setBounds (cell.reduced (4, 2));
    }
}
