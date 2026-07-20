#include "SongKeyEditor.h"

namespace
{
juce::String rootName (int pc) { static const char* n[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
                                 return n[((pc % 12) + 12) % 12]; }
}

SattariSongKeyAudioProcessorEditor::SattariSongKeyAudioProcessorEditor (SattariSongKeyAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (560, 360);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Key");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Master-track key scanner - publishes one key to the whole suite");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);
    addAndMakeVisible (sessionHud);

    addAndMakeVisible (publishToggle);
    addAndMakeVisible (manualToggle);
    publishAtt = std::make_unique<ButtonAttachment> (processor.parameters(), "publish", publishToggle);
    manualAtt  = std::make_unique<ButtonAttachment> (processor.parameters(), "manual",  manualToggle);

    setupCombo (manualRootBox,  SattariSongKeyAudioProcessor::rootChoices());
    setupCombo (manualScaleBox, SattariSongKeyAudioProcessor::scaleChoices());
    rootAtt  = std::make_unique<ComboAttachment> (processor.parameters(), "manualRoot",  manualRootBox);
    scaleAtt = std::make_unique<ComboAttachment> (processor.parameters(), "manualScale", manualScaleBox);

    rescanButton.onClick = [this] { processor.requestRescan(); };
    addAndMakeVisible (rescanButton);

    startTimerHz (12);
}

SattariSongKeyAudioProcessorEditor::~SattariSongKeyAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariSongKeyAudioProcessorEditor::setupCombo (juce::ComboBox& c, const juce::StringArray& items)
{
    c.addItemList (items, 1);
    addAndMakeVisible (c);
}

void SattariSongKeyAudioProcessorEditor::timerCallback()
{
    snapshot = processor.detected();
    repaint();
}

void SattariSongKeyAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    const auto accent = juce::Colour (sattari::brand::kAccent);
    auto r = keyArea.toFloat();
    g.setColour (juce::Colour (sattari::brand::kPanelDeep));
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);

    const bool manual = processor.parameters().getRawParameterValue ("manual")->load() > 0.5f;

    // Big key readout.
    juce::String big = "--", mode = "listening...";
    if (manual)
    {
        big  = rootName ((int) std::round (processor.parameters().getRawParameterValue ("manualRoot")->load()));
        mode = processor.parameters().getRawParameterValue ("manualScale")->load() > 0.5f ? "minor (manual)" : "major (manual)";
    }
    else if (snapshot.valid)
    {
        big  = rootName (snapshot.root);
        mode = snapshot.minor ? "minor" : "major";
    }

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (64.0f, juce::Font::bold));
    g.drawText (big, keyArea.withTrimmedBottom (keyArea.getHeight() / 3), juce::Justification::centred);
    g.setColour (accent);
    g.setFont (juce::FontOptions (20.0f));
    g.drawText (mode, keyArea.removeFromBottom (keyArea.getHeight() / 2).withTrimmedTop (10), juce::Justification::centredTop);

    // Confidence bar.
    auto barArea = keyArea.removeFromBottom (16).reduced (40, 4).toFloat();
    g.setColour (juce::Colour (sattari::brand::kPanel));
    g.fillRoundedRectangle (barArea, 3.0f);
    const float conf = juce::jlimit (0.0f, 1.0f, (float) snapshot.confidence * 3.0f);
    g.setColour (accent.withAlpha (0.85f));
    g.fillRoundedRectangle (barArea.withWidth (barArea.getWidth() * conf), 3.0f);

    // Publishing status dot.
    const bool pub = snapshot.publishing;
    g.setColour (pub ? juce::Colour (sattari::brand::kSuccess) : juce::Colour (sattari::brand::kTextFaint));
    auto dot = juce::Rectangle<float> (r.getX() + 14.0f, r.getY() + 12.0f, 9.0f, 9.0f);
    g.fillEllipse (dot);
    g.setColour (juce::Colour (pub ? sattari::brand::kTextPrimary : sattari::brand::kTextMuted));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (pub ? "Publishing to suite" : "Not publishing", (int) dot.getRight() + 6, (int) r.getY() + 8, 200, 16, juce::Justification::centredLeft);
}

void SattariSongKeyAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);

    auto top = area.removeFromTop (60);
    sessionHud.setBounds (top.removeFromRight (160).removeFromTop (24).reduced (0, 1));
    title.setBounds (top.removeFromTop (36));
    subtitle.setBounds (top.removeFromTop (22));
    area.removeFromTop (8);

    keyArea = area.removeFromTop (160);
    area.removeFromTop (14);

    auto controls = area.removeFromTop (30);
    publishToggle.setBounds (controls.removeFromLeft (150));
    controls.removeFromLeft (8);
    rescanButton.setBounds (controls.removeFromRight (90));

    area.removeFromTop (10);
    auto manualRow = area.removeFromTop (30);
    manualToggle.setBounds (manualRow.removeFromLeft (150));
    manualRow.removeFromLeft (8);
    manualRootBox.setBounds (manualRow.removeFromLeft (70));
    manualRow.removeFromLeft (8);
    manualScaleBox.setBounds (manualRow.removeFromLeft (100));
}
