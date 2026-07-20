#include "PluginEditor.h"

SattariMeterAudioProcessorEditor::SattariMeterAudioProcessorEditor (SattariMeterAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (560, 320);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Meter");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Loudness · true-peak · correlation");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);
    addAndMakeVisible (sessionHud);

    target.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    target.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
    target.setTextValueSuffix (" LUFS");
    addAndMakeVisible (target);
    targetAtt = std::make_unique<SliderAttachment> (processor.parameters(), "target", target);
    targetCap.setText ("TARGET", juce::dontSendNotification);
    targetCap.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    targetCap.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    targetCap.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (targetCap);

    resetBtn.onClick = [this] { processor.resetIntegrated(); };
    addAndMakeVisible (resetBtn);

    startTimerHz (15);
}

SattariMeterAudioProcessorEditor::~SattariMeterAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariMeterAudioProcessorEditor::timerCallback()
{
    r = processor.getReadout();
    repaint();
}

void SattariMeterAudioProcessorEditor::drawStat (juce::Graphics& g, juce::Rectangle<int> area,
                                                 const juce::String& label, const juce::String& value,
                                                 juce::Colour accent)
{
    g.setColour (juce::Colour (sattari::brand::kPanelRaised));
    g.fillRoundedRectangle (area.toFloat(), 10.0f);
    auto inner = area.reduced (12, 10);
    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (label, inner.removeFromTop (16), juce::Justification::centredLeft);
    g.setColour (accent);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText (value, inner, juce::Justification::centredLeft);
}

void SattariMeterAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    const auto accent = juce::Colour (sattari::brand::productAccent ("meter"));
    const auto warn   = juce::Colour (sattari::brand::kWarning);
    auto fmt = [] (float v) { return v <= -99.0f ? juce::String ("--") : juce::String (v, 1); };

    auto grid = juce::Rectangle<int> (18, 92, getWidth() - 36 - 120, 150);
    const int cw = grid.getWidth() / 2 - 6, chh = grid.getHeight() / 2 - 6;
    drawStat (g, { grid.getX(),            grid.getY(),            cw, chh }, "MOMENTARY", fmt (r.momentaryLufs) + " LUFS", accent);
    drawStat (g, { grid.getX() + cw + 12,  grid.getY(),            cw, chh }, "SHORT-TERM", fmt (r.shortLufs) + " LUFS", accent);
    drawStat (g, { grid.getX(),            grid.getY() + chh + 12, cw, chh }, "INTEGRATED", fmt (r.integratedLufs) + " LUFS", accent);
    drawStat (g, { grid.getX() + cw + 12,  grid.getY() + chh + 12, cw, chh }, "TRUE PEAK",
              r.truePeakDb <= -99.0f ? "--" : juce::String (r.truePeakDb, 1) + " dB",
              r.truePeakDb > -1.0f ? warn : accent);

    // Correlation bar (-1..+1) at the bottom.
    auto corrArea = juce::Rectangle<float> (18.0f, 258.0f, (float) getWidth() - 36.0f, 20.0f);
    g.setColour (juce::Colour (sattari::brand::kPanelRaised));
    g.fillRoundedRectangle (corrArea, 6.0f);
    const float mid = corrArea.getCentreX();
    const float x = mid + juce::jlimit (-1.0f, 1.0f, r.correlation) * (corrArea.getWidth() * 0.5f - 4.0f);
    g.setColour (r.correlation < 0.0f ? warn : accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (mid, x), corrArea.getY() + 3.0f,
                                                    std::fabs (x - mid), corrArea.getHeight() - 6.0f), 4.0f);
    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("CORRELATION " + juce::String (r.correlation, 2), corrArea.toNearestInt(), juce::Justification::centred);
}

void SattariMeterAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (18);
    sessionHud.setBounds (b.removeFromRight (156).removeFromTop (24).reduced (0, 1));
    title.setBounds (b.removeFromTop (34));
    subtitle.setBounds (b.removeFromTop (20));

    auto right = juce::Rectangle<int> (getWidth() - 110, 92, 92, 130);
    target.setBounds (right.removeFromTop (100));
    targetCap.setBounds (right.removeFromTop (16));
    resetBtn.setBounds (getWidth() - 150, 232, 132, 22);
}
