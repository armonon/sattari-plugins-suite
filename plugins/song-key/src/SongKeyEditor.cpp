#include "SongKeyEditor.h"

#include <cmath>

SattariSongKeyAudioProcessorEditor::SattariSongKeyAudioProcessorEditor (
    SattariSongKeyAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    setTitle ("Sattari Key");
    setDescription (
        "Focused key detection and fallback suite publication");
    setResizable (true, true);
    setResizeLimits (660, 470, 1080, 760);
    setSize (760, 500);

    addAndMakeVisible (header);
    addAndMakeVisible (sessionHud);
    addAndMakeVisible (confidenceMetric);
    addAndMakeVisible (tuningMetric);
    addAndMakeVisible (engineMetric);

    publishToggle.setTitle ("Publish to suite");
    publishToggle.setDescription (
        "Publish this detected or manual key to other Sattari products");
    addAndMakeVisible (publishToggle);
    manualToggle.setTitle ("Manual key override");
    manualToggle.setDescription (
        "Use an explicit key instead of the detected result");
    manualToggle.onStateChange = [this]
    {
        updateManualControls();
        updateSnapshot();
    };
    addAndMakeVisible (manualToggle);

    setupCombo (
        manualRootBox, SattariSongKeyAudioProcessor::rootChoices(),
        "Manual root");
    setupCombo (
        manualScaleBox, SattariSongKeyAudioProcessor::scaleChoices(),
        "Manual scale");

    publishAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.parameters(), "publish", publishToggle);
    manualAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.parameters(), "manual", manualToggle);
    rootAttachment = std::make_unique<ComboAttachment> (
        audioProcessor.parameters(), "manualRoot", manualRootBox);
    scaleAttachment = std::make_unique<ComboAttachment> (
        audioProcessor.parameters(), "manualScale", manualScaleBox);

    rescanButton.setTitle ("Re-scan song key");
    rescanButton.setDescription (
        "Clear the rolling decision and analyze the current material again");
    rescanButton.onClick = [this] { audioProcessor.requestRescan(); };
    addAndMakeVisible (rescanButton);

    updateManualControls();
    updateSnapshot();
    startTimerHz (12);
}

SattariSongKeyAudioProcessorEditor::~SattariSongKeyAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

juce::String SattariSongKeyAudioProcessorEditor::rootName (
    int pitchClass)
{
    static const char* names[] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    return names[((pitchClass % 12) + 12) % 12];
}

void SattariSongKeyAudioProcessorEditor::setupCombo (
    juce::ComboBox& combo, const juce::StringArray& items,
    const juce::String& title)
{
    combo.addItemList (items, 1);
    combo.setTitle (title);
    combo.setDescription ("Choose " + title.toLowerCase());
    addAndMakeVisible (combo);
}

void SattariSongKeyAudioProcessorEditor::timerCallback()
{
    updateSnapshot();
}

void SattariSongKeyAudioProcessorEditor::updateManualControls()
{
    const bool manual = manualToggle.getToggleState();
    manualRootBox.setEnabled (manual);
    manualScaleBox.setEnabled (manual);
}

juce::String SattariSongKeyAudioProcessorEditor::currentRoot() const
{
    if (manualToggle.getToggleState())
        return rootName ((int) std::round (
            audioProcessor.parameters()
                .getRawParameterValue ("manualRoot")->load()));
    return snapshot.valid ? rootName (snapshot.root) : juce::String ("--");
}

juce::String SattariSongKeyAudioProcessorEditor::currentMode() const
{
    if (manualToggle.getToggleState())
        return audioProcessor.parameters()
                       .getRawParameterValue ("manualScale")->load() > 0.5f
            ? "MINOR / MANUAL" : "MAJOR / MANUAL";
    if (! snapshot.valid) return "LISTENING";
    return snapshot.minor ? "MINOR" : "MAJOR";
}

void SattariSongKeyAudioProcessorEditor::updateSnapshot()
{
    snapshot = audioProcessor.detected();
    const bool manual = manualToggle.getToggleState();
    const float confidence = manual ? 1.0f : juce::jlimit (
        0.0f, 1.0f, (float) snapshot.confidence * 3.0f);
    confidenceMetric.setValue (
        juce::String ((int) std::round (confidence * 100.0f)),
        "%", manual ? "explicit key" : "current decision");
    tuningMetric.setValue (
        snapshot.valid ? juce::String (snapshot.detuneCents, 1)
                       : juce::String ("--"),
        "cents", "relative to A440");
    engineMetric.setValue (
        manual ? "MANUAL" : snapshot.neural ? "NEURAL" : "CHROMA",
        {}, manual ? "explicit override"
                   : snapshot.neural ? "spectrogram model"
                                     : "always available");

    juce::String status;
    bool active = false;
    if (snapshot.publishing)
    {
        status = manual ? "MANUAL / PUBLISHING" : "PUBLISHING TO SUITE";
        active = true;
    }
    else if (manual)
    {
        status = "MANUAL / LOCAL";
        active = true;
    }
    else if (snapshot.valid)
    {
        status = "KEY DETECTED";
        active = true;
    }
    else
    {
        status = "LISTENING";
    }
    header.setStatus (status, active);
    repaint();
}

void SattariSongKeyAudioProcessorEditor::paint (juce::Graphics& g)
{
    sattari::studio::paintCanvas (g, getLocalBounds(), accent);
    sattari::studio::paintSurface (g, keyArea.toFloat(), true);
    sattari::studio::paintSurface (g, manualArea.toFloat(), true);

    auto key = keyArea.reduced (16, 12);
    sattari::studio::paintSectionTitle (
        g, key.removeFromTop (18), "Detected key",
        snapshot.publishing ? "session fallback" : "local analysis");
    key.removeFromTop (4);

    auto rootArea = key.removeFromTop (
        juce::jmax (72, (int) std::round (key.getHeight() * 0.58f)));
    g.setColour (
        currentRoot() == "--"
            ? sattari::studio::colour::tertiary.withAlpha (0.6f)
            : sattari::studio::colour::text);
    g.setFont (sattari::brand::fontOptions (
        (float) juce::jlimit (
            66, 104, (int) std::round (rootArea.getHeight() * 0.92f)),
        juce::Font::bold));
    g.drawText (
        currentRoot(), rootArea,
        juce::Justification::centred, false);

    g.setColour (accent);
    g.setFont (sattari::brand::fontOptions (14.0f, juce::Font::bold));
    g.drawText (
        currentMode(), key.removeFromTop (24),
        juce::Justification::centred, false);

    key.removeFromTop (6);
    auto confidenceBar = key.removeFromTop (8).toFloat();
    g.setColour (sattari::studio::colour::separator);
    g.fillRoundedRectangle (confidenceBar, 4.0f);
    const bool manual = manualToggle.getToggleState();
    const float confidence = manual ? 1.0f : juce::jlimit (
        0.0f, 1.0f, (float) snapshot.confidence * 3.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (
        confidenceBar.withWidth (
            confidenceBar.getWidth() * confidence),
        4.0f);

    sattari::studio::paintSectionTitle (
        g, manualArea.reduced (14).removeFromTop (18),
        "Detection", "automatic or explicit key");
}

void SattariSongKeyAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (
        (int) sattari::studio::metric::outerMargin);
    header.setBounds (
        area.removeFromTop (sattari::studio::metric::headerHeight));

    auto command = area.removeFromTop (
        sattari::studio::metric::commandHeight);
    publishToggle.setBounds (command.removeFromLeft (166));
    command.removeFromLeft (8);
    rescanButton.setBounds (command.removeFromLeft (90));
    sessionHud.setBounds (
        command.removeFromRight (156).withSizeKeepingCentre (156, 26));

    area.removeFromTop ((int) sattari::studio::metric::gap);
    manualArea = area.removeFromBottom (72);
    area.removeFromBottom ((int) sattari::studio::metric::gap);

    const int gap = (int) sattari::studio::metric::gap;
    keyArea = area.removeFromLeft (
        juce::jlimit (360, 500, (int) std::round (
            (float) area.getWidth() * 0.66f)));
    area.removeFromLeft (gap);

    auto metrics = area;
    const int metricHeight = (metrics.getHeight() - gap * 2) / 3;
    confidenceMetric.setBounds (metrics.removeFromTop (metricHeight));
    metrics.removeFromTop (gap);
    tuningMetric.setBounds (metrics.removeFromTop (metricHeight));
    metrics.removeFromTop (gap);
    engineMetric.setBounds (metrics);

    auto manual = manualArea.reduced (14);
    manual.removeFromTop (24);
    manualToggle.setBounds (manual.removeFromLeft (158));
    manual.removeFromLeft (10);
    manualRootBox.setBounds (manual.removeFromLeft (110));
    manual.removeFromLeft (8);
    manualScaleBox.setBounds (manual.removeFromLeft (130));
}
