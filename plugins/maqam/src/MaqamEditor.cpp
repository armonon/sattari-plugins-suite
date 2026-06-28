#include "MaqamEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>

namespace
{
juce::String noteName (int pc)
{
    static const char* n[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return n[((pc % 12) + 12) % 12];
}
}

SattariMaqamAudioProcessorEditor::SattariMaqamAudioProcessorEditor (SattariMaqamAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      presetManager (p.parameters(), "maqam")
{
    setLookAndFeel (&lookAndFeel);
    setSize (820, 480);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Maqam");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Microtonal maqam tuner - quarter-tone correction for Arabic / Persian / Turkish modes");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    keyStatus.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (keyStatus);

    inputMeterLabel.setJustificationType (juce::Justification::centredRight);
    outputMeterLabel.setJustificationType (juce::Justification::centredRight);
    inputMeterLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    outputMeterLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent));
    addAndMakeVisible (inputMeterLabel);
    addAndMakeVisible (outputMeterLabel);

    setupCombo (maqamBox, SattariMaqamAudioProcessor::maqamChoices());
    setupCombo (rootBox,  SattariMaqamAudioProcessor::rootChoices());
    maqamAtt = std::make_unique<ComboAttachment> (processor.parameters(), "maqam", maqamBox);
    rootAtt  = std::make_unique<ComboAttachment> (processor.parameters(), "root",  rootBox);

    addAndMakeVisible (followKeyToggle);
    addAndMakeVisible (formantToggle);
    addAndMakeVisible (lowLatencyToggle);
    followAtt  = std::make_unique<ButtonAttachment> (processor.parameters(), "followKey",  followKeyToggle);
    formantAtt = std::make_unique<ButtonAttachment> (processor.parameters(), "formant",    formantToggle);
    lowLatAtt  = std::make_unique<ButtonAttachment> (processor.parameters(), "lowLatency", lowLatencyToggle);

    userPresets.setTextWhenNothingSelected ("User presets...");
    userPresets.onChange = [this] { const auto n = userPresets.getText(); if (n.isNotEmpty()) presetManager.loadUserPreset (n); };
    addAndMakeVisible (userPresets);
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (abRow);
    addAndMakeVisible (midiLearnButton);
    midiLearnButton.onClick = [this] { sattari::midi::showLearnPopup (processor.midiLearn, midiLearnButton, processor.parameters()); };
    refreshUserPresets();

    setupSlider (strengthSlider); strengthSlider.setName ("Strength");
    setupSlider (mixSlider);      mixSlider.setName ("Mix");
    sliderAtts.push_back (std::make_unique<SliderAttachment> (processor.parameters(), "strength", strengthSlider));
    sliderAtts.push_back (std::make_unique<SliderAttachment> (processor.parameters(), "mix",      mixSlider));

    startTimerHz (30);
}

SattariMaqamAudioProcessorEditor::~SattariMaqamAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariMaqamAudioProcessorEditor::setupSlider (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    addAndMakeVisible (s);
}

void SattariMaqamAudioProcessorEditor::setupCombo (juce::ComboBox& c, const juce::StringArray& items)
{
    c.addItemList (items, 1);   // populate before attachment (no blank dropdowns)
    addAndMakeVisible (c);
}

void SattariMaqamAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void SattariMaqamAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SattariMaqamAudioProcessorEditor::timerCallback()
{
    snapshot = processor.getSnapshot();
    inputMeterLabel.setText  ("IN "  + juce::String (juce::Decibels::gainToDecibels (processor.getInputMeter()  + 1.0e-6f), 1) + " dB", juce::dontSendNotification);
    outputMeterLabel.setText ("OUT " + juce::String (juce::Decibels::gainToDecibels (processor.getOutputMeter() + 1.0e-6f), 1) + " dB", juce::dontSendNotification);

    keyStatus.setText ((snapshot.followingBus ? "Following Auto Pitch root:  " : "Root:  ") + noteName (snapshot.rootPc),
                       juce::dontSendNotification);
    keyStatus.setColour (juce::Label::textColourId,
        juce::Colour (snapshot.followingBus ? sattari::brand::kAccent : sattari::brand::kTextMuted));
    repaint();
}

void SattariMaqamAudioProcessorEditor::drawLadder (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto r = area.toFloat();
    g.setColour (juce::Colour (sattari::brand::kPanelDeep));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    const auto accent  = juce::Colour (sattari::brand::kAccent);
    const auto cyan    = juce::Colour (0xff7dd3fc);
    const auto& mq     = sattari::maqam::maqam (snapshot.maqamIndex);

    auto inner = r.reduced (22.0f, 30.0f);
    const float x0 = inner.getX(), x1 = inner.getRight();
    const float axisY = inner.getBottom() - 10.0f;
    auto centsToX = [&] (double c) { return x0 + (float) (c / 1200.0) * (x1 - x0); };

    g.setColour (juce::Colour (sattari::brand::kTextFaint));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (juce::String (mq.name).toUpperCase() + " on " + noteName (snapshot.rootPc),
                area.reduced (14, 8).removeFromTop (16), juce::Justification::topLeft);

    // Axis.
    g.setColour (juce::Colour (sattari::brand::kHairline));
    g.drawLine (x0, axisY, x1, axisY, 1.0f);

    // Degree ticks — neutral (quarter-tone) degrees stand out in cyan.
    for (int i = 0; i < mq.numDegrees; ++i)
    {
        const int  c = mq.cents[i];
        const bool neutral = sattari::maqam::isNeutral (c);
        const float x = centsToX (c);
        g.setColour (neutral ? cyan : juce::Colour (sattari::brand::kTextMuted));
        g.drawLine (x, axisY - (neutral ? 30.0f : 22.0f), x, axisY + 4.0f, neutral ? 2.0f : 1.2f);
        if (neutral)
        {
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (juce::String (c) + "c", (int) x - 24, (int) (axisY - 44.0f), 48, 12, juce::Justification::centred);
        }
    }
    // Octave end tick.
    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.drawLine (x1, axisY - 22.0f, x1, axisY + 4.0f, 1.2f);

    if (! snapshot.voiced)
    {
        g.setColour (juce::Colour (sattari::brand::kTextFaint));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Sing a note to see it snap to the maqam", area, juce::Justification::centred);
        return;
    }

    // Detected → corrected.
    const float dx = centsToX (snapshot.detectedCents);
    const float cx = centsToX (snapshot.correctedCents);
    const float topY = inner.getY() + 6.0f;

    g.setColour (juce::Colours::white.withAlpha (0.30f));
    g.drawLine (dx, topY, dx, axisY, 1.0f);
    g.fillEllipse (dx - 4.0f, topY - 4.0f, 8.0f, 8.0f);

    g.setColour (accent);
    g.drawLine (cx, topY, cx, axisY, 2.0f);
    g.fillEllipse (cx - 5.0f, topY - 5.0f, 10.0f, 10.0f);

    // Correction arrow at the top.
    g.setColour (accent.withAlpha (0.7f));
    g.drawLine (dx, topY - 0.0f, cx, topY, 1.5f);

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (juce::String ((int) std::round (snapshot.correctedCents)) + "c",
                (int) cx - 24, (int) axisY + 6, 48, 14, juce::Justification::centred);
}

void SattariMaqamAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    auto caption = [&] (juce::Slider& s)
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (s.getName(), s.getBounds().withHeight (16).translated (0, -15), juce::Justification::centred);
    };
    caption (strengthSlider);
    caption (mixSlider);

    drawLadder (g, ladderArea);
}

void SattariMaqamAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);

    auto top = area.removeFromTop (66);
    auto titleCol = top.removeFromLeft (430);
    title.setBounds (titleCol.removeFromTop (38));
    subtitle.setBounds (titleCol.removeFromTop (24));
    auto meters = top.removeFromRight (150);
    inputMeterLabel.setBounds (meters.removeFromTop (24));
    outputMeterLabel.setBounds (meters.removeFromTop (24));

    auto presetRow = area.removeFromTop (32);
    userPresets.setBounds (presetRow.removeFromLeft (180));
    presetRow.removeFromLeft (8);
    savePresetButton.setBounds (presetRow.removeFromLeft (110));
    presetRow.removeFromLeft (8);
    abRow.setBounds (presetRow.removeFromLeft (94));
    presetRow.removeFromLeft (8);
    midiLearnButton.setBounds (presetRow.removeFromLeft (110));
    area.removeFromTop (10);

    auto modeRow = area.removeFromTop (30);
    maqamBox.setBounds (modeRow.removeFromLeft (150));
    modeRow.removeFromLeft (10);
    rootBox.setBounds (modeRow.removeFromLeft (70));
    modeRow.removeFromLeft (12);
    followKeyToggle.setBounds (modeRow.removeFromLeft (160));
    modeRow.removeFromLeft (8);
    keyStatus.setBounds (modeRow);
    area.removeFromTop (12);

    ladderArea = area.removeFromTop (150);
    area.removeFromTop (14);

    auto bottom = area.removeFromTop (120);
    auto knobs = bottom.removeFromLeft (300);
    strengthSlider.setBounds (knobs.removeFromLeft (140).reduced (8));
    mixSlider.setBounds      (knobs.removeFromLeft (140).reduced (8));

    auto toggles = bottom.removeFromLeft (220).reduced (8, 20);
    formantToggle.setBounds (toggles.removeFromTop (30));
    lowLatencyToggle.setBounds (toggles.removeFromTop (30));
}
