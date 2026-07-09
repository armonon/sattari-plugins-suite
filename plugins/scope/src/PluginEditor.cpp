#include "PluginEditor.h"
#include <cmath>

SattariScopeAudioProcessorEditor::SattariScopeAudioProcessorEditor (SattariScopeAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (620, 420);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Scope");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Spectrum · goniometer · correlation");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    tilt.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    tilt.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    tilt.setTextValueSuffix (" dB");
    addAndMakeVisible (tilt);
    tiltAtt = std::make_unique<SliderAttachment> (processor.parameters(), "tilt", tilt);
    tiltCap.setText ("TILT", juce::dontSendNotification);
    tiltCap.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    tiltCap.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    tiltCap.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (tiltCap);

    chromaToggle.setColour (juce::ToggleButton::textColourId, juce::Colour (sattari::brand::kTextMuted));
    addAndMakeVisible (chromaToggle);
    chromaAtt = std::make_unique<ButtonAttachment> (processor.parameters(), "chroma", chromaToggle);

    startTimerHz (30);
}

SattariScopeAudioProcessorEditor::~SattariScopeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariScopeAudioProcessorEditor::timerCallback()
{
    spec = processor.getSpectrum();
    vec  = processor.getVector();
    repaint();
}

void SattariScopeAudioProcessorEditor::drawSpectrum (juce::Graphics& g, juce::Rectangle<int> area,
                                                     juce::Colour accent)
{
    g.setColour (juce::Colour (sattari::brand::kPanelRaised));
    g.fillRoundedRectangle (area.toFloat(), 10.0f);
    auto inner = area.reduced (10, 8);

    // dB grid lines at -20, -40, -60, -80.
    g.setColour (juce::Colour (sattari::brand::kHairline));
    const float topDb = 6.0f, botDb = -100.0f;
    auto dbToY = [&] (float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - botDb) / (topDb - botDb));
        return (float) inner.getBottom() - t * (float) inner.getHeight();
    };
    for (float db = -20.0f; db > botDb; db -= 20.0f)
    {
        const float y = dbToY (db);
        g.drawHorizontalLine ((int) y, (float) inner.getX(), (float) inner.getRight());
    }

    if (! spec.hasData) { return; }

    // Spectrum area fill under the curve.
    juce::Path fill, line;
    const int N = SattariScopeAudioProcessor::kNumBands;
    const float w = (float) inner.getWidth();
    auto xFor = [&] (int b) { return (float) inner.getX() + w * (float) b / (float) (N - 1); };

    fill.startNewSubPath (xFor (0), (float) inner.getBottom());
    for (int b = 0; b < N; ++b)
    {
        const float x = xFor (b);
        const float y = dbToY (spec.bandDb[b]);
        if (b == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
        fill.lineTo (x, y);
    }
    fill.lineTo (xFor (N - 1), (float) inner.getBottom());
    fill.closeSubPath();

    g.setColour (accent.withAlpha (0.16f));
    g.fillPath (fill);
    g.setColour (accent);
    g.strokePath (line, juce::PathStrokeType (1.6f));

    // Optional pitch-class overlay: draw the key note names + chroma weight bars
    // along the bottom, and highlight the tonic.
    if (chromaToggle.getToggleState())
    {
        auto strip = juce::Rectangle<int> (inner.getX(), inner.getBottom() - 18, inner.getWidth(), 16);
        const float bw = (float) strip.getWidth() / 12.0f;
        for (int pc = 0; pc < 12; ++pc)
        {
            const float x  = (float) strip.getX() + bw * (float) pc;
            const float h  = juce::jlimit (0.0f, 1.0f, spec.chroma[pc]) * (float) strip.getHeight();
            const bool tonic = spec.keyValid && pc == spec.keyRoot;
            g.setColour ((tonic ? accent : juce::Colour (sattari::brand::kTextFaint))
                             .withAlpha (tonic ? 0.9f : 0.45f));
            g.fillRect (juce::Rectangle<float> (x + 1.0f, (float) strip.getBottom() - h, bw - 2.0f, h));
        }
    }
}

void SattariScopeAudioProcessorEditor::drawGonio (juce::Graphics& g, juce::Rectangle<int> area,
                                                  juce::Colour accent)
{
    g.setColour (juce::Colour (sattari::brand::kPanelRaised));
    g.fillRoundedRectangle (area.toFloat(), 10.0f);
    auto inner = area.reduced (10);
    const float cx = inner.getCentreX(), cy = inner.getCentreY();
    const float rad = (float) juce::jmin (inner.getWidth(), inner.getHeight()) * 0.5f;

    // Reference cross (mono = vertical, out-of-phase = horizontal).
    g.setColour (juce::Colour (sattari::brand::kHairline));
    g.drawLine (cx, cy - rad, cx, cy + rad, 1.0f);
    g.drawLine (cx - rad, cy, cx + rad, cy, 1.0f);
    g.drawEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f, 1.0f);

    // Point cloud: scale so a full-scale mono signal reaches the diamond edge.
    g.setColour (accent.withAlpha (0.65f));
    const float scale = rad;   // x,y already ~[-1,1]
    for (int i = 0; i < vec.gonioCount; ++i)
    {
        const float px = cx + juce::jlimit (-1.0f, 1.0f, vec.x[i]) * scale;
        const float py = cy - juce::jlimit (-1.0f, 1.0f, vec.y[i]) * scale;
        g.fillRect (px - 0.75f, py - 0.75f, 1.5f, 1.5f);
    }

    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("GONIOMETER", inner.removeFromTop (12), juce::Justification::centred);
}

void SattariScopeAudioProcessorEditor::drawCorrelation (juce::Graphics& g, juce::Rectangle<int> area,
                                                        juce::Colour accent)
{
    const auto warn = juce::Colour (sattari::brand::kWarning);
    auto bar = area.toFloat();
    g.setColour (juce::Colour (sattari::brand::kPanelRaised));
    g.fillRoundedRectangle (bar, 6.0f);
    const float mid = bar.getCentreX();
    const float c   = juce::jlimit (-1.0f, 1.0f, vec.correlation);
    const float x   = mid + c * (bar.getWidth() * 0.5f - 4.0f);
    g.setColour (c < 0.0f ? warn : accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (mid, x), bar.getY() + 3.0f,
                                                    std::fabs (x - mid), bar.getHeight() - 6.0f), 4.0f);
    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("CORRELATION " + juce::String (vec.correlation, 2), area, juce::Justification::centred);
}

void SattariScopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
    const auto accent = juce::Colour (sattari::brand::productAccent ("scope"));

    // Spectrum spans the top; goniometer + correlation sit below-left, key readout below-right.
    auto specArea = juce::Rectangle<int> (18, 96, getWidth() - 36 - 96, 190);
    drawSpectrum (g, specArea, accent);

    auto gonioArea = juce::Rectangle<int> (18, 296, 150, 108);
    drawGonio (g, gonioArea, accent);

    auto corrArea = juce::Rectangle<int> (178, 296, getWidth() - 178 - 18, 22);
    drawCorrelation (g, corrArea, accent);

    // Key readout under the correlation bar.
    auto keyArea = juce::Rectangle<int> (178, 326, getWidth() - 178 - 18, 78);
    g.setColour (juce::Colour (sattari::brand::kPanelRaised));
    g.fillRoundedRectangle (keyArea.toFloat(), 10.0f);
    auto ki = keyArea.reduced (14, 10);
    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("KEY (BUS)", ki.removeFromTop (16), juce::Justification::centredLeft);
    g.setColour (spec.keyValid ? accent : juce::Colour (sattari::brand::kTextFaint));
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    const juce::String keyStr = spec.keyValid
        ? juce::String (noteName (spec.keyRoot)) + (spec.keyScale == 1 ? " min" : " maj")
        : juce::String ("--");
    g.drawText (keyStr, ki, juce::Justification::centredLeft);
}

void SattariScopeAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (18);
    title.setBounds (b.removeFromTop (34));
    subtitle.setBounds (b.removeFromTop (20));

    auto right = juce::Rectangle<int> (getWidth() - 88, 96, 76, 118);
    tilt.setBounds (right.removeFromTop (94));
    tiltCap.setBounds (right.removeFromTop (16));

    chromaToggle.setBounds (getWidth() - 200, 226, 190, 22);
}
