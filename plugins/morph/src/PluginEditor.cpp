#include "PluginEditor.h"

namespace br = sattari::brand;

SattariMorphAudioProcessorEditor::SattariMorphAudioProcessorEditor (SattariMorphAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    title.setText ("Morph", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, juce::Colour (br::kTextPrimary));
    addAndMakeVisible (title);
    subtitle.setText (juce::String::fromUTF8 ("pitch \xc2\xb7 formant \xc2\xb7 character"), juce::dontSendNotification);
    subtitle.setColour (juce::Label::textColourId, juce::Colour (br::kTextMuted));
    addAndMakeVisible (subtitle);
    addAndMakeVisible (sessionHud);

    modeBox.addItemList ({ "Shift", "Quantize", "Robot" }, 1);
    addAndMakeVisible (modeBox);
    modeAtt = std::make_unique<CA> (processor.parameters(), "mode", modeBox);
    modeBox.onChange = [this] { resized(); };

    robotNoteBox.addItemList ({ "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 1);
    addAndMakeVisible (robotNoteBox);
    robotAtt = std::make_unique<CA> (processor.parameters(), "robotnote", robotNoteBox);
    robotCap.setText ("Robot note", juce::dontSendNotification);
    robotCap.setFont (juce::Font (juce::FontOptions (11.0f)));
    robotCap.setColour (juce::Label::textColourId, juce::Colour (br::kTextMuted));
    addAndMakeVisible (robotCap);

    knob (pitch, pitchC, "Pitch", "pitch", pitchA);
    knob (formant, formantC, "Formant", "formant", formantA);
    knob (drive, driveC, "Drive", "drive", driveA);
    knob (mix, mixC, "Mix", "mix", mixA);

    setSize (440, 250);
    startTimerHz (4);
}

SattariMorphAudioProcessorEditor::~SattariMorphAudioProcessorEditor() { setLookAndFeel (nullptr); }

void SattariMorphAudioProcessorEditor::knob (juce::Slider& s, juce::Label& c, const juce::String& name,
                                             const juce::String& id, std::unique_ptr<SA>& att)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 15);
    addAndMakeVisible (s);
    c.setText (name, juce::dontSendNotification);
    c.setJustificationType (juce::Justification::centred);
    c.setFont (juce::Font (juce::FontOptions (12.0f)));
    c.setColour (juce::Label::textColourId, juce::Colour (br::kTextMuted));
    addAndMakeVisible (c);
    att = std::make_unique<SA> (processor.parameters(), id, s);
}

void SattariMorphAudioProcessorEditor::timerCallback() {}

void SattariMorphAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (br::kPanel));
    g.setColour (juce::Colour (br::kPanelDeep));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (6.0f), 10.0f);
}

void SattariMorphAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto top = r.removeFromTop (30);
    title.setBounds (top.removeFromLeft (90));
    subtitle.setBounds (top.removeFromLeft (150).withTrimmedTop (7));
    sessionHud.setBounds (top.removeFromRight (150).withSizeKeepingCentre (150, 22));
    r.removeFromTop (10);

    auto modeRow = r.removeFromTop (26);
    modeBox.setBounds (modeRow.removeFromLeft (110));
    const bool robot = modeBox.getSelectedItemIndex() == 2;
    robotCap.setBounds (modeRow.removeFromLeft (70).withTrimmedTop (7));
    robotNoteBox.setBounds (modeRow.removeFromLeft (64));
    robotCap.setVisible (robot); robotNoteBox.setVisible (robot);
    r.removeFromTop (10);

    juce::Slider* ks[] = { &pitch, &formant, &drive, &mix };
    juce::Label*  cs[] = { &pitchC, &formantC, &driveC, &mixC };
    const int w = r.getWidth() / 4;
    for (int i = 0; i < 4; ++i) { auto cell = r.removeFromLeft (w); cs[i]->setBounds (cell.removeFromBottom (16)); ks[i]->setBounds (cell.reduced (4)); }
}
