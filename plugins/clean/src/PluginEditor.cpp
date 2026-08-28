#include "PluginEditor.h"

SattariCleanAudioProcessorEditor::SattariCleanAudioProcessorEditor (SattariCleanAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (480, 290);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Clean");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Adaptive multiband denoiser");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    struct K { juce::Slider* s; juce::Label* c; const char* id; const char* cap; std::unique_ptr<SA>* a; };
    K ks[] = {
        { &amount,      &amountCap, "amount",      "AMOUNT", &atts[0] },
        { &sensitivity, &sensCap,   "sensitivity", "SENS",   &atts[1] },
        { &output,      &outputCap, "output",      "OUTPUT", &atts[2] },
    };
    for (auto& k : ks)
    {
        styleKnob (*k.s); styleCaption (*k.c, k.cap);
        addAndMakeVisible (*k.s); addAndMakeVisible (*k.c);
        *k.a = std::make_unique<SA> (processor.parameters(), k.id, *k.s);
    }
    output.setTextValueSuffix (" dB");
    styleCaption (grCap, "NOISE REDUCTION");
    addAndMakeVisible (grCap);
    startTimerHz (24);
}

SattariCleanAudioProcessorEditor::~SattariCleanAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariCleanAudioProcessorEditor::styleKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 16);
}
void SattariCleanAudioProcessorEditor::styleCaption (juce::Label& l, const juce::String& t)
{
    l.setText (t, juce::dontSendNotification);
    l.setFont (sattari::brand::fontOptions (10.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    l.setJustificationType (juce::Justification::centred);
}

void SattariCleanAudioProcessorEditor::timerCallback()
{
    const float gr = -processor.getReductionDb();   // getReductionDb is negative dB
    grSmoothed += (gr - grSmoothed) * (gr > grSmoothed ? 0.5f : 0.15f);
    repaint();
}

void SattariCleanAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
    auto meter = juce::Rectangle<float> (18.0f, (float) getHeight() - 40.0f, (float) getWidth() - 36.0f, 18.0f);
    g.setColour (juce::Colour (sattari::brand::kPanelRaised));
    g.fillRoundedRectangle (meter, 5.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, grSmoothed / 24.0f);
    g.setColour (juce::Colour (sattari::brand::productAccent ("clean")));
    g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * frac), 5.0f);
}

void SattariCleanAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (18);
    title.setBounds (b.removeFromTop (34));
    subtitle.setBounds (b.removeFromTop (20));
    grCap.setBounds (18, getHeight() - 60, getWidth() - 36, 16);
    b.removeFromTop (14);

    juce::Slider* sl[] = { &amount, &sensitivity, &output };
    juce::Label*  ca[] = { &amountCap, &sensCap, &outputCap };
    auto row = b.removeFromTop (150);
    const int w = row.getWidth() / 3;
    for (int i = 0; i < 3; ++i)
    {
        auto cell = row.removeFromLeft (w);
        ca[i]->setBounds (cell.removeFromBottom (16));
        sl[i]->setBounds (cell.reduced (8, 0));
    }
}
