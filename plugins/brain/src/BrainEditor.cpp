#include "BrainEditor.h"

namespace
{
juce::String rootName (int pc) { static const char* n[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
                                 return n[((pc % 12) + 12) % 12]; }
}

SattariBrainAudioProcessorEditor::SattariBrainAudioProcessorEditor (SattariBrainAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (600, 640);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Brain");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Session brain - detects, edits & publishes the whole SessionState to the suite");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    // Master + detection controls.
    addAndMakeVisible (publishToggle);
    publishAtt = std::make_unique<ButtonAttachment> (processor.parameters(), "brainPublish", publishToggle);

    rescanButton.onClick = [this] { processor.requestRescan(); };
    addAndMakeVisible (rescanButton);

    midiLearnButton.onClick = [this]
    {
        sattari::midi::showLearnPopup (processor.midiLearn, midiLearnButton, processor.parameters());
    };
    addAndMakeVisible (midiLearnButton);

    // Manual key override.
    addAndMakeVisible (manualToggle);
    manualAtt = std::make_unique<ButtonAttachment> (processor.parameters(), "brainManual", manualToggle);
    setupCombo (manualRootBox,  SattariBrainAudioProcessor::rootChoices());
    setupCombo (manualScaleBox, SattariBrainAudioProcessor::scaleChoices());
    manualRootAtt  = std::make_unique<ComboAttachment> (processor.parameters(), "brainManualRoot",  manualRootBox);
    manualScaleAtt = std::make_unique<ComboAttachment> (processor.parameters(), "brainManualScale", manualScaleBox);

    // Reference tuning override.
    addAndMakeVisible (refOverrideToggle);
    refOverrideAtt = std::make_unique<ButtonAttachment> (processor.parameters(), "brainRefOverride", refOverrideToggle);
    styleCaption (capRef, "Reference A (Hz)");
    setupSlider (referenceSlider, " Hz");
    referenceAtt = std::make_unique<SliderAttachment> (processor.parameters(), "brainReferenceHz", referenceSlider);

    // Editable SessionState — combos.
    styleCaption (capChordRoot, "Chord Root");
    styleCaption (capChordQ,    "Chord Quality");
    styleCaption (capSection,   "Section");
    styleCaption (capTsDen,     "Time Sig Den");
    setupCombo (chordRootBox,    SattariBrainAudioProcessor::chordRootChoices());
    setupCombo (chordQualityBox, SattariBrainAudioProcessor::chordQualityChoices());
    setupCombo (sectionBox,      SattariBrainAudioProcessor::sectionChoices());
    setupCombo (timeSigDenBox,   SattariBrainAudioProcessor::timeSigDenChoices());
    chordRootAtt    = std::make_unique<ComboAttachment> (processor.parameters(), "brainChordRoot",    chordRootBox);
    chordQualityAtt = std::make_unique<ComboAttachment> (processor.parameters(), "brainChordQuality", chordQualityBox);
    sectionAtt      = std::make_unique<ComboAttachment> (processor.parameters(), "brainSection",      sectionBox);
    timeSigDenAtt   = std::make_unique<ComboAttachment> (processor.parameters(), "brainTimeSigDen",   timeSigDenBox);

    // Editable SessionState — sliders.
    styleCaption (capTsNum,  "Time Sig Num");
    styleCaption (capSwing,  "Swing");
    styleCaption (capEnergy, "Energy");
    styleCaption (capMaqam,  "Maqam Id");
    setupSlider (timeSigNumSlider);
    setupSlider (swingSlider);
    setupSlider (energySlider);
    setupSlider (maqamSlider);
    timeSigNumAtt = std::make_unique<SliderAttachment> (processor.parameters(), "brainTimeSigNum", timeSigNumSlider);
    swingAtt      = std::make_unique<SliderAttachment> (processor.parameters(), "brainSwing",      swingSlider);
    energyAtt     = std::make_unique<SliderAttachment> (processor.parameters(), "brainEnergy",     energySlider);
    maqamAtt      = std::make_unique<SliderAttachment> (processor.parameters(), "brainMaqam",      maqamSlider);

    startTimerHz (12);
}

SattariBrainAudioProcessorEditor::~SattariBrainAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariBrainAudioProcessorEditor::setupCombo (juce::ComboBox& c, const juce::StringArray& items)
{
    c.addItemList (items, 1);
    addAndMakeVisible (c);
}

void SattariBrainAudioProcessorEditor::setupSlider (juce::Slider& s, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    s.setTextValueSuffix (suffix);
    addAndMakeVisible (s);
}

void SattariBrainAudioProcessorEditor::styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (sattari::brand::kLabelSize));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
}

void SattariBrainAudioProcessorEditor::timerCallback()
{
    snapshot = processor.detected();
    repaint();
}

void SattariBrainAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    const auto accent = juce::Colour (sattari::brand::kAccent);
    auto r = keyArea.toFloat();
    g.setColour (juce::Colour (sattari::brand::kPanelDeep));
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);

    const bool manual = processor.parameters().getRawParameterValue ("brainManual")->load() > 0.5f;

    // Big key readout.
    juce::String big = "--", mode = "listening...";
    if (manual)
    {
        big  = rootName ((int) std::round (processor.parameters().getRawParameterValue ("brainManualRoot")->load()));
        mode = processor.parameters().getRawParameterValue ("brainManualScale")->load() > 0.5f ? "minor (manual)" : "major (manual)";
    }
    else if (snapshot.valid)
    {
        big  = rootName (snapshot.root);
        mode = snapshot.minor ? "minor" : "major";
    }

    auto keyBox = keyArea;
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (54.0f, juce::Font::bold));
    g.drawText (big, keyBox.removeFromTop (74).withTrimmedTop (8), juce::Justification::centred);
    g.setColour (accent);
    g.setFont (juce::FontOptions (18.0f));
    g.drawText (mode, keyBox.removeFromTop (22), juce::Justification::centred);

    // Confidence bar.
    auto barArea = keyBox.removeFromTop (14).reduced (48, 3).toFloat();
    g.setColour (juce::Colour (sattari::brand::kPanel));
    g.fillRoundedRectangle (barArea, 3.0f);
    const float conf = juce::jlimit (0.0f, 1.0f, (float) snapshot.confidence * 3.0f);
    g.setColour (accent.withAlpha (0.85f));
    g.fillRoundedRectangle (barArea.withWidth (barArea.getWidth() * conf), 3.0f);

    // Reference / tempo / time-signature line.
    const bool refOverride = processor.parameters().getRawParameterValue ("brainRefOverride")->load() > 0.5f;
    const double refHz = refOverride
        ? (double) processor.parameters().getRawParameterValue ("brainReferenceHz")->load()
        : 440.0 * std::pow (2.0, snapshot.detuneCents / 1200.0);
    juce::String info = "A = " + juce::String (refHz, 1) + " Hz";
    if (snapshot.bpm > 0.0) info += "   -   " + juce::String (snapshot.bpm, 1) + " BPM";
    if (snapshot.tsNum > 0 && snapshot.tsDen > 0)
        info += "   -   host " + juce::String (snapshot.tsNum) + "/" + juce::String (snapshot.tsDen);
    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (info, keyBox.removeFromTop (20), juce::Justification::centred);

    // Publishing status dot.
    const bool pub = snapshot.publishing;
    g.setColour (pub ? juce::Colour (sattari::brand::kSuccess) : juce::Colour (sattari::brand::kTextFaint));
    auto dot = juce::Rectangle<float> (r.getX() + 14.0f, r.getY() + 12.0f, 9.0f, 9.0f);
    g.fillEllipse (dot);
    g.setColour (juce::Colour (pub ? sattari::brand::kTextPrimary : sattari::brand::kTextMuted));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (pub ? "Publishing SessionState" : "Not publishing",
                (int) dot.getRight() + 6, (int) r.getY() + 8, 220, 16, juce::Justification::centredLeft);
}

void SattariBrainAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (18);

    auto top = area.removeFromTop (58);
    title.setBounds (top.removeFromTop (34));
    subtitle.setBounds (top.removeFromTop (20));
    area.removeFromTop (6);

    // Status row: publish (left), rescan + MIDI learn (right).
    auto statusRow = area.removeFromTop (28);
    publishToggle.setBounds (statusRow.removeFromLeft (180));
    midiLearnButton.setBounds (statusRow.removeFromRight (96));
    statusRow.removeFromRight (8);
    rescanButton.setBounds (statusRow.removeFromRight (84));
    area.removeFromTop (8);

    keyArea = area.removeFromTop (150);
    area.removeFromTop (12);

    // Manual key override.
    auto manualRow = area.removeFromTop (28);
    manualToggle.setBounds (manualRow.removeFromLeft (150));
    manualRow.removeFromLeft (8);
    manualRootBox.setBounds (manualRow.removeFromLeft (70));
    manualRow.removeFromLeft (8);
    manualScaleBox.setBounds (manualRow.removeFromLeft (100));
    area.removeFromTop (10);

    // Reference override.
    auto refRow = area.removeFromTop (28);
    refOverrideToggle.setBounds (refRow.removeFromLeft (120));
    capRef.setBounds (refRow.removeFromLeft (110));
    referenceSlider.setBounds (refRow);
    area.removeFromTop (12);

    // Helper: a caption + control on one row.
    auto labelledRow = [&area] (juce::Label& cap, juce::Component& ctrl, int rowH = 26)
    {
        auto row = area.removeFromTop (rowH);
        cap.setBounds (row.removeFromLeft (110));
        ctrl.setBounds (row);
        area.removeFromTop (8);
    };

    labelledRow (capChordRoot, chordRootBox);
    labelledRow (capChordQ,    chordQualityBox);
    labelledRow (capSection,   sectionBox);

    // Time signature: numerator slider + denominator combo on one row.
    {
        auto row = area.removeFromTop (26);
        capTsNum.setBounds (row.removeFromLeft (110));
        auto denBox = row.removeFromRight (150);
        capTsDen.setBounds (denBox.removeFromLeft (86));
        timeSigDenBox.setBounds (denBox);
        timeSigNumSlider.setBounds (row.removeFromLeft (row.getWidth() - 8));
        area.removeFromTop (8);
    }

    labelledRow (capSwing,  swingSlider);
    labelledRow (capEnergy, energySlider);
    labelledRow (capMaqam,  maqamSlider);
}
