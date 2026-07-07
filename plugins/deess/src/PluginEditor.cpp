#include "PluginEditor.h"

SattariDeEssAudioProcessorEditor::SattariDeEssAudioProcessorEditor (SattariDeEssAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (600, 300);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari De-Ess");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Dynamic sibilance control");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    auto& params = processor.parameters();
    struct K { juce::Slider* s; juce::Label* c; const char* id; const char* cap;
               std::unique_ptr<SliderAttachment>* att; };
    K ks[] = {
        { &freq,      &freqCap,   "freq",      "FREQ",   &freqAtt },
        { &threshold, &threshCap, "threshold", "THRESH", &threshAtt },
        { &range,     &rangeCap,  "range",     "RANGE",  &rangeAtt },
        { &output,    &outputCap, "output",    "OUTPUT", &outputAtt },
    };
    for (auto& k : ks)
    {
        styleKnob (*k.s);
        styleCaption (*k.c, k.cap);
        addAndMakeVisible (*k.s);
        addAndMakeVisible (*k.c);
        *k.att = std::make_unique<SliderAttachment> (params, k.id, *k.s);
    }
    freq.setTextValueSuffix (" Hz");
    threshold.setTextValueSuffix (" dB");
    range.setTextValueSuffix (" dB");
    output.setTextValueSuffix (" dB");

    mode.addItemList ({ "Split", "Wideband" }, 1);
    addAndMakeVisible (mode);
    modeAtt = std::make_unique<ComboAttachment> (params, "mode", mode);
    addAndMakeVisible (listen);
    listenAtt = std::make_unique<ButtonAttachment> (params, "listen", listen);

    styleCaption (grCap, "GAIN REDUCTION");
    addAndMakeVisible (grCap);

    startTimerHz (30);
}

SattariDeEssAudioProcessorEditor::~SattariDeEssAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariDeEssAudioProcessorEditor::styleKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 16);
}

void SattariDeEssAudioProcessorEditor::styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    l.setJustificationType (juce::Justification::centred);
}

void SattariDeEssAudioProcessorEditor::timerCallback()
{
    const float gr = processor.getReductionDb();
    grSmoothed += (gr - grSmoothed) * (gr > grSmoothed ? 0.6f : 0.15f);
    repaint();
}

void SattariDeEssAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    // Gain-reduction bar (0..24 dB, drawn downward).
    auto meter = juce::Rectangle<float> (getWidth() - 60.0f, 96.0f, 30.0f, 150.0f);
    g.setColour (juce::Colour (sattari::brand::kPanelRaised));
    g.fillRoundedRectangle (meter, 4.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, grSmoothed / 24.0f);
    auto fill = meter.withTop (meter.getY()).withHeight (meter.getHeight() * frac);
    g.setColour (juce::Colour (sattari::brand::productAccent ("deess")));
    g.fillRoundedRectangle (fill, 4.0f);
}

void SattariDeEssAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (18);
    title.setBounds (b.removeFromTop (34));
    subtitle.setBounds (b.removeFromTop (20));
    b.removeFromTop (14);

    auto row = b.removeFromTop (150).withTrimmedRight (80);
    juce::Slider* sl[] = { &freq, &threshold, &range, &output };
    juce::Label*  ca[] = { &freqCap, &threshCap, &rangeCap, &outputCap };
    const int w = row.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = row.removeFromLeft (w);
        ca[i]->setBounds (cell.removeFromBottom (16));
        sl[i]->setBounds (cell.reduced (4, 0));
    }
    grCap.setBounds (getWidth() - 92, 76, 92, 16);

    b.removeFromTop (8);
    auto bottom = b.removeFromTop (30).withTrimmedRight (80);
    mode.setBounds (bottom.removeFromLeft (150));
    bottom.removeFromLeft (16);
    listen.setBounds (bottom.removeFromLeft (100));
}
