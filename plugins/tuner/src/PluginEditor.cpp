#include "PluginEditor.h"

#include <cmath>

SattariTunerAudioProcessorEditor::SattariTunerAudioProcessorEditor (
    SattariTunerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    setTitle ("Sattari Tuner");
    setDescription (
        "Precision tuner with reference, smoothing and transposition controls");
    setResizable (true, true);
    setResizeLimits (620, 460, 1040, 760);
    setSize (720, 520);

    addAndMakeVisible (header);
    addAndMakeVisible (sessionHud);
    addAndMakeVisible (referenceA);
    addAndMakeVisible (smoothing);
    addAndMakeVisible (transpose);
    for (auto* control : { &referenceA, &smoothing, &transpose })
    {
        control->slider().setSliderStyle (juce::Slider::LinearHorizontal);
        control->slider().setTextBoxStyle (
            juce::Slider::TextBoxRight, false, 68, 20);
    }

    factoryPreset.setTitle ("Factory preset");
    factoryPreset.setDescription (
        "Choose a tuning reference and response preset");
    for (int i = 0; i < audioProcessor.getNumPrograms(); ++i)
        factoryPreset.addItem (audioProcessor.getProgramName (i), i + 1);
    factoryPreset.setSelectedId (
        audioProcessor.getCurrentProgram() + 1, juce::dontSendNotification);
    factoryPreset.onChange = [this]
    {
        const int program = factoryPreset.getSelectedItemIndex();
        if (program >= 0) audioProcessor.setCurrentProgram (program);
    };
    addAndMakeVisible (factoryPreset);

    auto& parameters = audioProcessor.parameters();
    referenceAttachment = std::make_unique<SliderAttachment> (
        parameters, "referenceA", referenceA.slider());
    smoothingAttachment = std::make_unique<SliderAttachment> (
        parameters, "smoothing", smoothing.slider());
    transposeAttachment = std::make_unique<SliderAttachment> (
        parameters, "transpose", transpose.slider());

    updateReading();
    startTimerHz (30);
}

SattariTunerAudioProcessorEditor::~SattariTunerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariTunerAudioProcessorEditor::timerCallback()
{
    updateReading();
}

void SattariTunerAudioProcessorEditor::updateReading()
{
    reading = audioProcessor.getReading();
    const float target = reading.voiced
        ? juce::jlimit (-50.0f, 50.0f, reading.cents) : 0.0f;
    needleCents += (target - needleCents)
        * (reading.voiced ? 0.35f : 0.12f);

    const bool inTune =
        reading.voiced && std::abs (reading.cents) < 5.0f;
    tuneGlow += ((inTune ? 1.0f : 0.0f) - tuneGlow) * 0.2f;
    header.setStatus (
        reading.voiced
            ? SattariTunerAudioProcessor::noteName (reading.midiNote)
                + " / "
                + juce::String ((int) std::round (reading.cents))
                + " CENTS"
            : "WAITING FOR NOTE",
        reading.voiced);
    repaint();
}

void SattariTunerAudioProcessorEditor::paintTunerFace (
    juce::Graphics& g, juce::Rectangle<int> bounds)
{
    auto area = sattari::studio::analysis::paintPlotFrame (
        g, bounds, "Pitch center",
        reading.voiced
            ? juce::String ((int) std::round (reading.confidence * 100.0f))
                + "% confidence"
            : juce::String ("listening"));
    const bool inTune =
        reading.voiced && std::abs (reading.cents) < 5.0f;
    const auto liveColour = inTune
        ? sattari::studio::colour::success.interpolatedWith (
              accent, 1.0f - tuneGlow)
        : accent;

    auto noteArea = area.removeFromTop (
        juce::jmax (62, (int) std::round (area.getHeight() * 0.48f)));
    g.setColour (
        reading.voiced ? liveColour
                       : sattari::studio::colour::tertiary.withAlpha (0.55f));
    g.setFont (sattari::brand::fontOptions (
        (float) juce::jlimit (
            54, 92, (int) std::round (noteArea.getHeight() * 0.88f)),
        juce::Font::bold));
    g.drawText (
        reading.voiced
            ? SattariTunerAudioProcessor::noteName (reading.midiNote)
            : juce::String ("--"),
        noteArea, juce::Justification::centred, false);

    auto scale = area.removeFromTop (50).reduced (10, 0);
    const float centre = scale.getCentreX();
    const float halfWidth = scale.getWidth() * 0.5f - 8.0f;
    for (int cents = -50; cents <= 50; cents += 10)
    {
        const float x = centre + (float) cents / 50.0f * halfWidth;
        const bool zero = cents == 0;
        g.setColour (
            zero ? sattari::studio::colour::success
                 : sattari::studio::colour::separator);
        const float height = zero ? 22.0f : 11.0f;
        g.fillRoundedRectangle (
            x - (zero ? 1.5f : 0.75f), (float) scale.getY(),
            zero ? 3.0f : 1.5f, height, 1.0f);
    }

    g.setColour (
        sattari::studio::colour::success.withAlpha (
            0.10f + 0.14f * tuneGlow));
    g.fillRect (
        centre - halfWidth * 0.1f, (float) scale.getY(),
        halfWidth * 0.2f, 26.0f);

    const float needleX = centre + juce::jlimit (
        -1.0f, 1.0f, needleCents / 50.0f) * halfWidth;
    g.setColour (
        reading.voiced ? liveColour
                       : sattari::studio::colour::tertiary);
    g.fillRoundedRectangle (
        needleX - 2.0f, (float) scale.getY() - 4.0f,
        4.0f, 42.0f, 2.0f);

    auto readout = area.reduced (10, 0);
    g.setFont (sattari::brand::fontOptions (12.0f, juce::Font::bold));
    g.setColour (
        reading.voiced ? sattari::studio::colour::secondary
                       : sattari::studio::colour::tertiary);
    if (reading.voiced)
    {
        const int cents = (int) std::round (reading.cents);
        const juce::String direction =
            cents <= -3 ? "FLAT" : cents >= 3 ? "SHARP" : "IN TUNE";
        g.drawText (
            juce::String (cents > 0 ? "+" : "")
                + juce::String (cents)
                + " cents   /   "
                + juce::String (reading.frequencyHz, 1)
                + " Hz   /   " + direction,
            readout, juce::Justification::centred, false);
    }
    else
    {
        g.drawText (
            "Play a sustained note",
            readout, juce::Justification::centred, false);
    }
}

void SattariTunerAudioProcessorEditor::paint (juce::Graphics& g)
{
    sattari::studio::paintCanvas (g, getLocalBounds(), accent);
    paintTunerFace (g, tunerArea);
    sattari::studio::paintSurface (
        g, controlsArea.toFloat(), true);
    sattari::studio::paintSectionTitle (
        g, controlsArea.reduced (14).removeFromTop (18),
        "Calibration", "host-automation ready");
}

void SattariTunerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (
        (int) sattari::studio::metric::outerMargin);
    header.setBounds (
        area.removeFromTop (sattari::studio::metric::headerHeight));

    auto command = area.removeFromTop (
        sattari::studio::metric::commandHeight);
    factoryPreset.setBounds (command.removeFromLeft (210));
    sessionHud.setBounds (
        command.removeFromRight (156).withSizeKeepingCentre (156, 26));

    area.removeFromTop ((int) sattari::studio::metric::gap);
    controlsArea = area.removeFromBottom (
        juce::jlimit (108, 128, area.getHeight() / 3));
    area.removeFromBottom ((int) sattari::studio::metric::gap);
    tunerArea = area;

    auto controls = controlsArea.reduced (14);
    controls.removeFromTop (24);
    const int gap = (int) sattari::studio::metric::gap;
    const int width = (controls.getWidth() - gap * 2) / 3;
    referenceA.setBounds (controls.removeFromLeft (width));
    controls.removeFromLeft (gap);
    smoothing.setBounds (controls.removeFromLeft (width));
    controls.removeFromLeft (gap);
    transpose.setBounds (controls);
}
