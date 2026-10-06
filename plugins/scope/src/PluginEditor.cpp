#include "PluginEditor.h"

#include <cmath>

SattariScopeAudioProcessorEditor::SattariScopeAudioProcessorEditor (
    SattariScopeAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    setTitle ("Sattari Scope");
    setDescription (
        "Compact spectrum, stereo-field and pitch-class analysis");
    setResizable (true, true);
    setResizeLimits (720, 480, 1280, 900);
    setSize (860, 560);

    addAndMakeVisible (header);
    addAndMakeVisible (keyMetric);

    sattari::studio::styleFieldLabel (tiltLabel, "Spectrum tilt");
    addAndMakeVisible (tiltLabel);
    tilt.setSliderStyle (juce::Slider::LinearHorizontal);
    tilt.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
    tilt.setTextValueSuffix (" dB");
    tilt.setTitle ("Spectrum tilt");
    tilt.setDescription ("Adjust the visual spectrum slope");
    addAndMakeVisible (tilt);
    tiltAttachment = std::make_unique<SliderAttachment> (
        audioProcessor.parameters(), "tilt", tilt);

    chromaToggle.setTitle ("Pitch-class overlay");
    chromaToggle.setDescription (
        "Show pitch-class energy beneath the spectrum");
    addAndMakeVisible (chromaToggle);
    chromaAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.parameters(), "chroma", chromaToggle);

    updateSnapshots();
    startTimerHz (30);
}

SattariScopeAudioProcessorEditor::~SattariScopeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariScopeAudioProcessorEditor::timerCallback()
{
    updateSnapshots();
}

void SattariScopeAudioProcessorEditor::updateSnapshots()
{
    spectrum = audioProcessor.getSpectrum();
    vector = audioProcessor.getVector();
    const auto key = spectrum.keyValid
        ? juce::String (noteName (spectrum.keyRoot))
              + (spectrum.keyScale == 1 ? " minor" : " major")
        : juce::String ("--");
    keyMetric.setValue (
        key, {}, spectrum.keyValid ? "from suite bus" : "no published key");
    header.setStatus (
        spectrum.hasData ? "ANALYSIS LIVE" : "WAITING FOR AUDIO",
        spectrum.hasData);
    repaint();
}

void SattariScopeAudioProcessorEditor::drawSpectrum (
    juce::Graphics& g, juce::Rectangle<int> bounds)
{
    auto inner = sattari::studio::analysis::paintPlotFrame (
        g, bounds, "Spectrum", "20 Hz / 20 kHz");
    const float topDb = 6.0f;
    const float bottomDb = -100.0f;
    const auto dbToY = [&inner, topDb, bottomDb] (float db)
    {
        const float normalized = juce::jlimit (
            0.0f, 1.0f, (db - bottomDb) / (topDb - bottomDb));
        return (float) inner.getBottom()
            - normalized * (float) inner.getHeight();
    };

    g.setColour (sattari::studio::colour::separator.withAlpha (0.7f));
    for (float db = -20.0f; db > bottomDb; db -= 20.0f)
        g.drawHorizontalLine (
            (int) dbToY (db), (float) inner.getX(),
            (float) inner.getRight());

    if (! spectrum.hasData)
    {
        sattari::studio::analysis::paintWaitingState (
            g, inner, "Play audio to begin spectrum analysis");
        return;
    }

    constexpr int count = SattariScopeAudioProcessor::kNumBands;
    const auto xFor = [&inner, bandCount = count] (int band)
    {
        return (float) inner.getX()
            + (float) inner.getWidth() * (float) band / (float) (bandCount - 1);
    };

    juce::Path fill;
    juce::Path line;
    fill.startNewSubPath (xFor (0), (float) inner.getBottom());
    for (int band = 0; band < count; ++band)
    {
        const float x = xFor (band);
        const float y = dbToY (spectrum.bandDb[band]);
        if (band == 0) line.startNewSubPath (x, y);
        else line.lineTo (x, y);
        fill.lineTo (x, y);
    }
    fill.lineTo (xFor (count - 1), (float) inner.getBottom());
    fill.closeSubPath();
    g.setColour (accent.withAlpha (0.13f));
    g.fillPath (fill);
    g.setColour (accent);
    g.strokePath (
        line, juce::PathStrokeType (
                  1.7f, juce::PathStrokeType::curved,
                  juce::PathStrokeType::rounded));

    if (! chromaToggle.getToggleState()) return;
    auto strip = inner.removeFromBottom (20);
    const float width = (float) strip.getWidth() / 12.0f;
    for (int pc = 0; pc < 12; ++pc)
    {
        const float x = (float) strip.getX() + width * (float) pc;
        const float height = juce::jlimit (
            0.0f, 1.0f, spectrum.chroma[pc])
            * (float) strip.getHeight();
        const bool tonic = spectrum.keyValid && pc == spectrum.keyRoot;
        g.setColour (
            (tonic ? accent : sattari::studio::colour::tertiary)
                .withAlpha (tonic ? 0.9f : 0.42f));
        g.fillRect (
            x + 1.0f, (float) strip.getBottom() - height,
            width - 2.0f, height);
    }
}

void SattariScopeAudioProcessorEditor::drawGoniometer (
    juce::Graphics& g, juce::Rectangle<int> bounds)
{
    auto inner = sattari::studio::analysis::paintPlotFrame (
        g, bounds, "Stereo field", "mid / side");
    const float centreX = inner.getCentreX();
    const float centreY = inner.getCentreY();
    const float radius =
        (float) juce::jmin (inner.getWidth(), inner.getHeight()) * 0.45f;

    g.setColour (sattari::studio::colour::separator);
    g.drawLine (
        centreX, centreY - radius, centreX, centreY + radius, 1.0f);
    g.drawLine (
        centreX - radius, centreY, centreX + radius, centreY, 1.0f);
    g.drawEllipse (
        centreX - radius, centreY - radius,
        radius * 2.0f, radius * 2.0f, 1.0f);

    if (vector.gonioCount == 0)
    {
        sattari::studio::analysis::paintWaitingState (
            g, inner, "waiting");
        return;
    }

    g.setColour (accent.withAlpha (0.68f));
    for (int i = 0; i < vector.gonioCount; ++i)
    {
        const float x = centreX + juce::jlimit (
            -1.0f, 1.0f, vector.x[i]) * radius;
        const float y = centreY - juce::jlimit (
            -1.0f, 1.0f, vector.y[i]) * radius;
        g.fillEllipse (x - 1.0f, y - 1.0f, 2.0f, 2.0f);
    }
}

void SattariScopeAudioProcessorEditor::drawCorrelation (
    juce::Graphics& g, juce::Rectangle<int> bounds)
{
    sattari::studio::paintSurface (g, bounds.toFloat(), true);
    auto inner = bounds.reduced (14, 9);
    sattari::studio::paintSectionTitle (
        g, inner.removeFromTop (16), "Correlation",
        vector.correlation < 0.0f ? "phase risk" : "stable");
    inner.removeFromTop (7);
    sattari::studio::analysis::paintBipolarBar (
        g, inner.removeFromTop (20).toFloat(),
        vector.correlation, accent, vector.correlation < 0.0f,
        juce::String (vector.correlation, 2));
}

void SattariScopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    sattari::studio::paintCanvas (g, getLocalBounds(), accent);
    drawSpectrum (g, spectrumArea);
    drawGoniometer (g, goniometerArea);
    drawCorrelation (g, correlationArea);
}

void SattariScopeAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (
        (int) sattari::studio::metric::outerMargin);
    header.setBounds (
        area.removeFromTop (sattari::studio::metric::headerHeight));

    auto command = area.removeFromTop (
        sattari::studio::metric::commandHeight);
    tiltLabel.setBounds (command.removeFromLeft (92));
    tilt.setBounds (command.removeFromLeft (240));
    command.removeFromLeft (16);
    chromaToggle.setBounds (command.removeFromLeft (190));

    area.removeFromTop ((int) sattari::studio::metric::gap);
    const int spectrumHeight = juce::jlimit (
        170, 250, (int) std::round ((float) area.getHeight() * 0.58f));
    spectrumArea = area.removeFromTop (spectrumHeight);
    area.removeFromTop ((int) sattari::studio::metric::gap);

    const int gap = (int) sattari::studio::metric::gap;
    const int gonioWidth = juce::jmin (
        220, juce::jmax (150, (int) std::round (
            (float) area.getHeight() * 1.25f)));
    goniometerArea = area.removeFromLeft (gonioWidth);
    area.removeFromLeft (gap);
    auto right = area;
    const int correlationHeight = juce::jmin (62, right.getHeight() / 2);
    correlationArea = right.removeFromBottom (correlationHeight);
    right.removeFromBottom (gap);
    keyMetric.setBounds (right);
}
