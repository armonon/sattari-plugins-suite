#include "PluginEditor.h"

SattariTransientAudioProcessorEditor::SattariTransientAudioProcessorEditor (SattariTransientAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (480, 290);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Transient");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Attack / sustain designer");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    struct K { juce::Slider* s; juce::Label* c; const char* id; const char* cap; };
    const K ks[] = {
        { &attack,  &attackCap,  "attack",  "ATTACK" },
        { &sustain, &sustainCap, "sustain", "SUSTAIN" },
        { &output,  &outputCap,  "output",  "OUTPUT" },
    };
    int i = 0;
    for (auto& k : ks)
    {
        styleKnob (*k.s);
        styleCaption (*k.c, k.cap);
        addAndMakeVisible (*k.s);
        addAndMakeVisible (*k.c);
        atts[(size_t) i++] = std::make_unique<SliderAttachment> (processor.parameters(), k.id, *k.s);
    }
    output.setTextValueSuffix (" dB");
}

SattariTransientAudioProcessorEditor::~SattariTransientAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SattariTransientAudioProcessorEditor::styleKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
}

void SattariTransientAudioProcessorEditor::styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    l.setJustificationType (juce::Justification::centred);
}

void SattariTransientAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariTransientAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (18);
    title.setBounds (b.removeFromTop (34));
    subtitle.setBounds (b.removeFromTop (20));
    b.removeFromTop (18);

    juce::Slider* sl[] = { &attack, &sustain, &output };
    juce::Label*  ca[] = { &attackCap, &sustainCap, &outputCap };
    auto row = b.removeFromTop (150);
    const int w = row.getWidth() / 3;
    for (int i = 0; i < 3; ++i)
    {
        auto cell = row.removeFromLeft (w);
        ca[i]->setBounds (cell.removeFromBottom (16));
        sl[i]->setBounds (cell.reduced (8, 0));
    }
}
