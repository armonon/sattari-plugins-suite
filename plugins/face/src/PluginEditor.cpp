#include "PluginEditor.h"

SattariFaceAudioProcessorEditor::SattariFaceAudioProcessorEditor (SattariFaceAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (540, 300);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Face");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Formant / gender / character shifter");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    struct K { juce::Slider* s; juce::Label* c; const char* id; const char* cap; };
    const K ks[] = {
        { &pitch,   &pitchCap,   "pitch",   "PITCH" },
        { &formant, &formantCap, "formant", "FORMANT" },
        { &mix,     &mixCap,     "mix",     "MIX" },
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
    pitch.setTextValueSuffix (" st");
    formant.setTextValueSuffix (" st");
    output.setTextValueSuffix (" dB");
}

SattariFaceAudioProcessorEditor::~SattariFaceAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SattariFaceAudioProcessorEditor::styleKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
}

void SattariFaceAudioProcessorEditor::styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    l.setJustificationType (juce::Justification::centred);
}

void SattariFaceAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariFaceAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (18);
    title.setBounds (b.removeFromTop (34));
    subtitle.setBounds (b.removeFromTop (20));
    b.removeFromTop (18);

    juce::Slider* sl[] = { &pitch, &formant, &mix, &output };
    juce::Label*  ca[] = { &pitchCap, &formantCap, &mixCap, &outputCap };
    auto row = b.removeFromTop (150);
    const int w = row.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = row.removeFromLeft (w);
        ca[i]->setBounds (cell.removeFromBottom (16));
        sl[i]->setBounds (cell.reduced (6, 0));
    }
}
