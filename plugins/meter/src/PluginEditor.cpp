#include "PluginEditor.h"

SattariMeterAudioProcessorEditor::SattariMeterAudioProcessorEditor (
    SattariMeterAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    setTitle ("Sattari Meter");
    setDescription (
        "Compact loudness, true-peak and stereo-correlation analysis");
    setResizable (true, true);
    setResizeLimits (620, 380, 1000, 640);
    setSize (720, 410);

    addAndMakeVisible (header);
    addAndMakeVisible (momentary);
    addAndMakeVisible (shortTerm);
    addAndMakeVisible (integrated);
    addAndMakeVisible (truePeak);
    addAndMakeVisible (sessionHud);

    sattari::studio::styleFieldLabel (targetLabel, "Target");
    addAndMakeVisible (targetLabel);
    target.setSliderStyle (juce::Slider::LinearHorizontal);
    target.setTextBoxStyle (juce::Slider::TextBoxRight, false, 74, 20);
    target.setTextValueSuffix (" LUFS");
    target.setTitle ("Loudness target");
    target.setDescription ("Set the integrated loudness target");
    addAndMakeVisible (target);
    targetAttachment = std::make_unique<SliderAttachment> (
        audioProcessor.parameters(), "target", target);

    resetButton.setTitle ("Reset integrated loudness");
    resetButton.setDescription (
        "Clear the accumulated integrated loudness measurement");
    resetButton.onClick = [this]
    {
        audioProcessor.resetIntegrated();
        updateReadout();
    };
    addAndMakeVisible (resetButton);

    updateReadout();
    startTimerHz (15);
}

SattariMeterAudioProcessorEditor::~SattariMeterAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariMeterAudioProcessorEditor::timerCallback()
{
    updateReadout();
}

void SattariMeterAudioProcessorEditor::updateReadout()
{
    readout = audioProcessor.getReadout();
    const auto format = [] (float value)
    {
        return value <= -99.0f ? juce::String ("--")
                               : juce::String (value, 1);
    };

    momentary.setValue (format (readout.momentaryLufs), "LUFS", "400 ms");
    shortTerm.setValue (format (readout.shortLufs), "LUFS", "3 second window");
    integrated.setValue (
        format (readout.integratedLufs), "LUFS", "program average");
    truePeak.setValue (
        format (readout.truePeakDb), "dBTP", "4x oversampled",
        readout.truePeakDb > -1.0f);

    const bool active = readout.momentaryLufs > -99.0f;
    header.setStatus (
        active ? (readout.truePeakDb > -1.0f ? "PEAK WARNING" : "ANALYSIS LIVE")
               : "WAITING FOR AUDIO",
        active);
    repaint();
}

void SattariMeterAudioProcessorEditor::paint (juce::Graphics& g)
{
    sattari::studio::paintCanvas (g, getLocalBounds(), accent);
    sattari::studio::paintSurface (
        g, correlationArea.toFloat(), true);

    auto area = correlationArea.reduced (14, 10);
    sattari::studio::paintSectionTitle (
        g, area.removeFromTop (16), "Stereo correlation",
        readout.correlation < 0.0f ? "phase risk" : "mono-compatible");
    area.removeFromTop (5);
    sattari::studio::analysis::paintBipolarBar (
        g, area.removeFromTop (20).toFloat(), readout.correlation,
        accent, readout.correlation < 0.0f,
        juce::String (readout.correlation, 2));
}

void SattariMeterAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (
        (int) sattari::studio::metric::outerMargin);
    header.setBounds (
        area.removeFromTop (sattari::studio::metric::headerHeight));

    auto command = area.removeFromTop (
        sattari::studio::metric::commandHeight);
    sessionHud.setBounds (
        command.removeFromRight (156).withSizeKeepingCentre (156, 26));
    command.removeFromRight (8);
    targetLabel.setBounds (command.removeFromLeft (58));
    resetButton.setBounds (command.removeFromRight (130));
    command.removeFromRight (8);
    target.setBounds (command);

    area.removeFromTop ((int) sattari::studio::metric::gap);
    correlationArea = area.removeFromBottom (58);
    area.removeFromBottom ((int) sattari::studio::metric::gap);

    const int gap = (int) sattari::studio::metric::gap;
    const int width = (area.getWidth() - gap * 3) / 4;
    momentary.setBounds (area.removeFromLeft (width));
    area.removeFromLeft (gap);
    shortTerm.setBounds (area.removeFromLeft (width));
    area.removeFromLeft (gap);
    integrated.setBounds (area.removeFromLeft (width));
    area.removeFromLeft (gap);
    truePeak.setBounds (area);
}
