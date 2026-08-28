#include "PluginEditor.h"

SattariDoubleAudioProcessorEditor::SattariDoubleAudioProcessorEditor (SattariDoubleAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (620, 320);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Double");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Natural vocal doubler & thickener");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    struct K { juce::Slider* s; juce::Label* c; const char* id; const char* cap; };
    const K ks[] = {
        { &detune, &detuneCap, "detune", "DETUNE" },
        { &timing, &timingCap, "timing", "TIMING" },
        { &drift,  &driftCap,  "drift",  "DRIFT" },
        { &width,  &widthCap,  "width",  "WIDTH" },
        { &level,  &levelCap,  "level",  "DOUBLES" },
        { &output, &outputCap, "output", "OUTPUT" },
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
    detune.setTextValueSuffix (" ct");
    timing.setTextValueSuffix (" ms");
    output.setTextValueSuffix (" dB");
}

SattariDoubleAudioProcessorEditor::~SattariDoubleAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SattariDoubleAudioProcessorEditor::styleKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 16);
}

void SattariDoubleAudioProcessorEditor::styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (sattari::brand::fontOptions (10.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    l.setJustificationType (juce::Justification::centred);
}

void SattariDoubleAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariDoubleAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (18);
    title.setBounds (b.removeFromTop (34));
    subtitle.setBounds (b.removeFromTop (20));
    b.removeFromTop (18);

    juce::Slider* sl[] = { &detune, &timing, &drift, &width, &level, &output };
    juce::Label*  ca[] = { &detuneCap, &timingCap, &driftCap, &widthCap, &levelCap, &outputCap };
    auto row = b.removeFromTop (150);
    const int w = row.getWidth() / 6;
    for (int i = 0; i < 6; ++i)
    {
        auto cell = row.removeFromLeft (w);
        ca[i]->setBounds (cell.removeFromBottom (16));
        sl[i]->setBounds (cell.reduced (4, 0));
    }
}
