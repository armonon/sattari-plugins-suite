#include "PluginEditor.h"

namespace
{
void styleKnob (juce::Slider& s, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    s.setTextValueSuffix (suffix);
}

void styleCap (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (sattari::brand::fontOptions (10.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    l.setJustificationType (juce::Justification::centred);
}
}

SattariPeakAudioProcessorEditor::SattariPeakAudioProcessorEditor (SattariPeakAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (600, 360);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Peak");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "True-peak brickwall limiter · maximizer");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    auto& params = processor.parameters();

    styleKnob (push, " dB");        addAndMakeVisible (push);
    styleKnob (ceiling, " dBTP");   addAndMakeVisible (ceiling);
    styleKnob (release, " ms");     addAndMakeVisible (release);
    styleKnob (lookahead, " ms");   addAndMakeVisible (lookahead);
    styleKnob (targetLufs, " LUFS"); addAndMakeVisible (targetLufs);

    styleCap (pushCap,   "PUSH");    addAndMakeVisible (pushCap);
    styleCap (ceilCap,   "CEILING"); addAndMakeVisible (ceilCap);
    styleCap (relCap,    "RELEASE"); addAndMakeVisible (relCap);
    styleCap (lookCap,   "LOOKAHEAD"); addAndMakeVisible (lookCap);
    styleCap (targetCap, "TARGET");  addAndMakeVisible (targetCap);

    addAndMakeVisible (truePeak);
    addAndMakeVisible (autoLoud);

    pushAtt   = std::make_unique<SliderAttachment> (params, "push", push);
    ceilAtt   = std::make_unique<SliderAttachment> (params, "ceiling", ceiling);
    relAtt    = std::make_unique<SliderAttachment> (params, "release", release);
    lookAtt   = std::make_unique<SliderAttachment> (params, "lookahead", lookahead);
    targetAtt = std::make_unique<SliderAttachment> (params, "targetLufs", targetLufs);
    tpAtt     = std::make_unique<ButtonAttachment> (params, "truePeak", truePeak);
    autoAtt   = std::make_unique<ButtonAttachment> (params, "autoLoud", autoLoud);

    startTimerHz (15);
}

SattariPeakAudioProcessorEditor::~SattariPeakAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariPeakAudioProcessorEditor::timerCallback()
{
    r = processor.getReadout();
    repaint();
}

void SattariPeakAudioProcessorEditor::drawStat (juce::Graphics& g, juce::Rectangle<int> area,
                                                const juce::String& label, const juce::String& value,
                                                juce::Colour accent)
{
    g.setColour (juce::Colour (sattari::brand::kPanelRaised));
    g.fillRoundedRectangle (area.toFloat(), 10.0f);
    auto inner = area.reduced (12, 8);
    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.setFont (sattari::brand::fontOptions (10.0f, juce::Font::bold));
    g.drawText (label, inner.removeFromTop (14), juce::Justification::centredLeft);
    g.setColour (accent);
    g.setFont (sattari::brand::fontOptions (22.0f, juce::Font::bold));
    g.drawText (value, inner, juce::Justification::centredLeft);
}

void SattariPeakAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    const auto accent = juce::Colour (sattari::brand::productAccent ("peak"));
    const auto warn   = juce::Colour (sattari::brand::kWarning);
    auto fmt = [] (float v) { return v <= -99.0f ? juce::String ("--") : juce::String (v, 1); };

    // Live readout row along the bottom.
    auto row = juce::Rectangle<int> (18, 258, getWidth() - 36, 84);
    const int cw = row.getWidth() / 4 - 6;
    const int chh = row.getHeight();

    drawStat (g, { row.getX(),                 row.getY(), cw, chh }, "GAIN REDUCTION",
              juce::String (r.gainReductionDb, 1) + " dB",
              r.gainReductionDb > 0.1f ? warn : accent);
    drawStat (g, { row.getX() + (cw + 8),      row.getY(), cw, chh }, "TRUE PEAK OUT",
              r.truePeakDb <= -99.0f ? "--" : juce::String (r.truePeakDb, 1) + " dB",
              r.truePeakDb > -0.05f ? warn : accent);
    drawStat (g, { row.getX() + 2 * (cw + 8),  row.getY(), cw, chh }, "SHORT LUFS",
              fmt (r.shortLufs), accent);
    drawStat (g, { row.getX() + 3 * (cw + 8),  row.getY(), cw, chh }, "PUSH",
              juce::String (r.pushDb, 1) + " dB", accent);
}

void SattariPeakAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (18);
    title.setBounds (b.removeFromTop (34));
    subtitle.setBounds (b.removeFromTop (20));

    // Five knobs across the top region.
    const int knobW = 92, knobH = 92, capH = 16;
    const int y = 96;
    const int gap = (getWidth() - 36 - 5 * knobW) / 4;
    int x = 18;
    auto place = [&] (juce::Slider& s, juce::Label& cap)
    {
        s.setBounds (x, y, knobW, knobH);
        cap.setBounds (x, y + knobH, knobW, capH);
        x += knobW + gap;
    };
    place (push, pushCap);
    place (ceiling, ceilCap);
    place (release, relCap);
    place (lookahead, lookCap);
    place (targetLufs, targetCap);

    // Toggles under the knob row.
    truePeak.setBounds (18, 224, 150, 24);
    autoLoud.setBounds (180, 224, 170, 24);
}
