#include "BrainEditor.h"

#include <cmath>

namespace
{
juce::String rootName (int pc)
{
    static const char* names[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    return names[((pc % 12) + 12) % 12];
}
}

SattariBrainAudioProcessorEditor::SattariBrainAudioProcessorEditor (
    SattariBrainAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    setResizable (true, true);
    setResizeLimits (760, 590, 1280, 900);
    setSize (960, 680);
    setTitle ("Sattari Compass");
    setDescription ("Authoritative musical context for the Sattari suite");

    addAndMakeVisible (header);

    publishToggle.setTitle ("Publish session context");
    publishToggle.setDescription (
        "Make Compass the active suite session authority");
    addAndMakeVisible (publishToggle);
    publishAtt = std::make_unique<ButtonAttachment> (
        audioProcessor.parameters(), "brainPublish", publishToggle);

    rescanButton.setTitle ("Analyse now");
    rescanButton.setDescription ("Request a new musical analysis");
    rescanButton.onClick = [this] { audioProcessor.requestRescan(); };
    addAndMakeVisible (rescanButton);

    midiLearnButton.onClick = [this]
    {
        sattari::midi::showLearnPopup (
            audioProcessor.midiLearn, midiLearnButton, audioProcessor.parameters());
    };
    addAndMakeVisible (midiLearnButton);

    addAndMakeVisible (manualToggle);
    manualAtt = std::make_unique<ButtonAttachment> (
        audioProcessor.parameters(), "brainManual", manualToggle);
    setupCaption (capManualRoot, "Root");
    setupCaption (capManualScale, "Mode");
    setupCombo (manualRootBox, SattariBrainAudioProcessor::rootChoices(), "Manual root");
    setupCombo (manualScaleBox, SattariBrainAudioProcessor::scaleChoices(), "Manual mode");
    manualRootAtt = std::make_unique<ComboAttachment> (
        audioProcessor.parameters(), "brainManualRoot", manualRootBox);
    manualScaleAtt = std::make_unique<ComboAttachment> (
        audioProcessor.parameters(), "brainManualScale", manualScaleBox);

    addAndMakeVisible (refOverrideToggle);
    refOverrideAtt = std::make_unique<ButtonAttachment> (
        audioProcessor.parameters(), "brainRefOverride", refOverrideToggle);
    setupCaption (capRef, "Reference A");
    setupSlider (referenceSlider, "Reference A", " Hz");
    referenceAtt = std::make_unique<SliderAttachment> (
        audioProcessor.parameters(), "brainReferenceHz", referenceSlider);

    setupCaption (capChordRoot, "Chord root");
    setupCaption (capChordQ, "Quality");
    setupCaption (capSection, "Section");
    setupCaption (capTsNum, "Meter");
    setupCaption (capTsDen, "Division");
    setupCaption (capSwing, "Swing");
    setupCaption (capEnergy, "Energy");
    setupCaption (capMaqam, "Maqam");

    setupCombo (chordRootBox, SattariBrainAudioProcessor::chordRootChoices(), "Chord root");
    setupCombo (chordQualityBox, SattariBrainAudioProcessor::chordQualityChoices(), "Chord quality");
    setupCombo (sectionBox, SattariBrainAudioProcessor::sectionChoices(), "Arrangement section");
    setupCombo (timeSigDenBox, SattariBrainAudioProcessor::timeSigDenChoices(), "Time signature denominator");
    setupSlider (timeSigNumSlider, "Time signature numerator");
    setupSlider (swingSlider, "Session swing");
    setupSlider (energySlider, "Arrangement energy");
    setupSlider (maqamSlider, "Maqam identifier");

    auto& params = audioProcessor.parameters();
    chordRootAtt = std::make_unique<ComboAttachment> (
        params, "brainChordRoot", chordRootBox);
    chordQualityAtt = std::make_unique<ComboAttachment> (
        params, "brainChordQuality", chordQualityBox);
    sectionAtt = std::make_unique<ComboAttachment> (
        params, "brainSection", sectionBox);
    timeSigDenAtt = std::make_unique<ComboAttachment> (
        params, "brainTimeSigDen", timeSigDenBox);
    timeSigNumAtt = std::make_unique<SliderAttachment> (
        params, "brainTimeSigNum", timeSigNumSlider);
    swingAtt = std::make_unique<SliderAttachment> (
        params, "brainSwing", swingSlider);
    energyAtt = std::make_unique<SliderAttachment> (
        params, "brainEnergy", energySlider);
    maqamAtt = std::make_unique<SliderAttachment> (
        params, "brainMaqam", maqamSlider);

    addAndMakeVisible (tempoMetric);
    addAndMakeVisible (tuningMetric);
    addAndMakeVisible (meterMetric);

    startTimerHz (15);
    timerCallback();
}

SattariBrainAudioProcessorEditor::~SattariBrainAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariBrainAudioProcessorEditor::setupCombo (
    juce::ComboBox& box, const juce::StringArray& items, const juce::String& title)
{
    box.addItemList (items, 1);
    box.setTitle (title);
    box.setDescription ("Choose " + title.toLowerCase());
    addAndMakeVisible (box);
}

void SattariBrainAudioProcessorEditor::setupSlider (
    juce::Slider& slider, const juce::String& title, const juce::String& suffix)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 22);
    slider.setTextValueSuffix (suffix);
    slider.setTitle (title);
    slider.setDescription ("Adjust " + title.toLowerCase());
    addAndMakeVisible (slider);
}

void SattariBrainAudioProcessorEditor::setupCaption (
    juce::Label& label, const juce::String& text)
{
    sattari::studio::styleFieldLabel (label, text);
    addAndMakeVisible (label);
}

void SattariBrainAudioProcessorEditor::timerCallback()
{
    snapshot = audioProcessor.detected();
    const bool manual = audioProcessor.parameters()
                            .getRawParameterValue ("brainManual")->load() > 0.5f;
    const bool publishing = snapshot.publishing;
    header.setStatus (publishing ? "SUITE AUTHORITY" : "LOCAL ONLY", publishing);

    tempoMetric.setValue (
        snapshot.bpm > 0.0 ? juce::String (snapshot.bpm, 1) : "--",
        snapshot.bpm > 0.0 ? "BPM" : "waiting for host");

    const bool refOverride = audioProcessor.parameters()
                                 .getRawParameterValue ("brainRefOverride")->load() > 0.5f;
    const double ref = refOverride
        ? (double) audioProcessor.parameters()
              .getRawParameterValue ("brainReferenceHz")->load()
        : 440.0 * std::pow (2.0, snapshot.detuneCents / 1200.0);
    tuningMetric.setValue (juce::String (ref, 1), refOverride ? "Hz · manual" : "Hz · detected");

    meterMetric.setValue (
        snapshot.tsNum > 0 && snapshot.tsDen > 0
            ? juce::String (snapshot.tsNum) + "/" + juce::String (snapshot.tsDen)
            : "--",
        snapshot.tsNum > 0 ? "host meter" : "not reported");

    manualRootBox.setEnabled (manual);
    manualScaleBox.setEnabled (manual);
    referenceSlider.setEnabled (refOverride);
    repaint();
}

void SattariBrainAudioProcessorEditor::paint (juce::Graphics& g)
{
    sattari::studio::paintCanvas (g, getLocalBounds(), accent);
    sattari::studio::paintSurface (g, heroArea.toFloat(), true);
    sattari::studio::paintSurface (g, tuningArea.toFloat());
    sattari::studio::paintSurface (g, contextArea.toFloat(), true);

    sattari::studio::paintSectionTitle (
        g, tuningArea.reduced (16).removeFromTop (18),
        "Overrides", "explicit values win over analysis");
    sattari::studio::paintSectionTitle (
        g, contextArea.reduced (16).removeFromTop (18),
        "Session context", "one coherent publish");

    paintSessionHero (g);
}

void SattariBrainAudioProcessorEditor::paintSessionHero (juce::Graphics& g)
{
    const bool manual = audioProcessor.parameters()
                            .getRawParameterValue ("brainManual")->load() > 0.5f;
    juce::String key = "--";
    juce::String mode = "LISTENING";
    if (manual)
    {
        const int root = (int) std::round (audioProcessor.parameters()
                                               .getRawParameterValue ("brainManualRoot")->load());
        key = rootName (root);
        mode = audioProcessor.parameters()
                   .getRawParameterValue ("brainManualScale")->load() > 0.5f
             ? "MINOR · MANUAL" : "MAJOR · MANUAL";
    }
    else if (snapshot.valid)
    {
        key = rootName (snapshot.root);
        mode = snapshot.minor ? "MINOR · DETECTED" : "MAJOR · DETECTED";
    }

    auto hero = heroArea.reduced (22);
    auto top = hero.removeFromTop (34);
    g.setColour (sattari::studio::colour::tertiary);
    g.setFont (sattari::brand::fontOptions (10.5f, juce::Font::bold));
    g.drawText ("CURRENT MUSICAL CONTEXT", top,
                juce::Justification::centredLeft, false);

    const float dialSize = juce::jmin (190.0f, (float) hero.getHeight() - 82.0f);
    auto dial = juce::Rectangle<float> (dialSize, dialSize)
                    .withCentre ({ (float) hero.getCentreX(),
                                   (float) hero.getY() + dialSize * 0.5f + 4.0f });

    g.setColour (sattari::studio::colour::surfaceHigh);
    g.fillEllipse (dial);
    g.setColour (sattari::studio::colour::separator);
    g.drawEllipse (dial.reduced (0.5f), 1.0f);

    const float confidence = manual
        ? 1.0f
        : juce::jlimit (0.0f, 1.0f, (float) snapshot.confidence * 3.0f);
    juce::Path track, value;
    const auto centre = dial.getCentre();
    const float radius = dial.getWidth() * 0.5f - 7.0f;
    const float start = juce::MathConstants<float>::pi * 0.75f;
    const float end = juce::MathConstants<float>::pi * 2.25f;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                         start, start + confidence * (end - start), true);
    g.setColour (sattari::studio::colour::separator);
    g.strokePath (track, juce::PathStrokeType (4.0f));
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (
        4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (sattari::studio::colour::text);
    g.setFont (sattari::brand::fontOptions (58.0f, juce::Font::bold));
    g.drawText (key, dial.withTrimmedBottom (35.0f).toNearestInt(),
                juce::Justification::centred, false);
    g.setColour (accent);
    g.setFont (sattari::brand::fontOptions (10.5f, juce::Font::bold));
    g.drawText (mode, dial.withTrimmedTop (112.0f).toNearestInt(),
                juce::Justification::centredTop, false);

    const juce::String confidenceText = manual
        ? "EXPLICIT AUTHORITY"
        : snapshot.valid
            ? juce::String ((int) std::round (confidence * 100.0f)) + "% CONFIDENCE"
            : "ANALYSING AUDIO";
    g.setColour (sattari::studio::colour::secondary);
    g.setFont (sattari::brand::fontOptions (10.0f, juce::Font::bold));
    g.drawText (confidenceText,
                juce::Rectangle<int> (hero.getX(), (int) dial.getBottom() + 6,
                                      hero.getWidth(), 18),
                juce::Justification::centred, false);
}

void SattariBrainAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced ((int) sattari::studio::metric::outerMargin);
    header.setBounds (area.removeFromTop (sattari::studio::metric::headerHeight));

    auto command = area.removeFromTop (sattari::studio::metric::commandHeight);
    publishToggle.setBounds (command.removeFromLeft (170));
    midiLearnButton.setBounds (command.removeFromRight (104));
    command.removeFromRight (8);
    rescanButton.setBounds (command.removeFromRight (112));
    area.removeFromTop ((int) sattari::studio::metric::gap);

    auto left = area.removeFromLeft ((int) std::round (area.getWidth() * 0.57));
    area.removeFromLeft ((int) sattari::studio::metric::gap);
    contextArea = area;

    heroArea = left.removeFromTop (juce::jmax (280, left.getHeight() - 190));
    left.removeFromTop ((int) sattari::studio::metric::gap);
    tuningArea = left;

    auto metrics = heroArea.reduced (16);
    metrics = metrics.removeFromBottom (64);
    const int metricGap = 8;
    const int metricWidth = (metrics.getWidth() - metricGap * 2) / 3;
    tempoMetric.setBounds (metrics.removeFromLeft (metricWidth));
    metrics.removeFromLeft (metricGap);
    tuningMetric.setBounds (metrics.removeFromLeft (metricWidth));
    metrics.removeFromLeft (metricGap);
    meterMetric.setBounds (metrics);

    auto overrides = tuningArea.reduced (16);
    overrides.removeFromTop (28);
    auto manualRow = overrides.removeFromTop (52);
    manualToggle.setBounds (manualRow.removeFromLeft (142).withSizeKeepingCentre (142, 26));
    auto rootCell = manualRow.removeFromLeft (90);
    capManualRoot.setBounds (rootCell.removeFromTop (16));
    manualRootBox.setBounds (rootCell);
    manualRow.removeFromLeft (8);
    auto scaleCell = manualRow.removeFromLeft (120);
    capManualScale.setBounds (scaleCell.removeFromTop (16));
    manualScaleBox.setBounds (scaleCell);

    overrides.removeFromTop (8);
    auto refRow = overrides.removeFromTop (52);
    refOverrideToggle.setBounds (
        refRow.removeFromLeft (142).withSizeKeepingCentre (142, 26));
    capRef.setBounds (refRow.removeFromTop (16));
    referenceSlider.setBounds (refRow);

    auto context = contextArea.reduced (16);
    context.removeFromTop (30);
    const int rowGap = 10;
    const int cellGap = 10;
    const int rowHeight = juce::jmax (52, (context.getHeight() - rowGap * 3) / 4);

    auto placePair = [&context, rowHeight] (
        juce::Label& aLabel, juce::Component& a,
        juce::Label& bLabel, juce::Component& b)
    {
        auto row = context.removeFromTop (rowHeight);
        const int width = (row.getWidth() - cellGap) / 2;
        auto leftCell = row.removeFromLeft (width);
        row.removeFromLeft (cellGap);
        auto rightCell = row;
        aLabel.setBounds (leftCell.removeFromTop (17));
        a.setBounds (leftCell.removeFromTop (32));
        bLabel.setBounds (rightCell.removeFromTop (17));
        b.setBounds (rightCell.removeFromTop (32));
        context.removeFromTop (10);
    };

    placePair (capChordRoot, chordRootBox, capChordQ, chordQualityBox);
    placePair (capSection, sectionBox, capMaqam, maqamSlider);
    placePair (capTsNum, timeSigNumSlider, capTsDen, timeSigDenBox);
    placePair (capSwing, swingSlider, capEnergy, energySlider);
}
