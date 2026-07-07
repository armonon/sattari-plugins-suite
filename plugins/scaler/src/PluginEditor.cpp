#include "PluginEditor.h"

SattariScalerAudioProcessorEditor::SattariScalerAudioProcessorEditor (SattariScalerAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (560, 300);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Scaler");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Key-following chord & scale generator");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    {
        sattari::ui::KeyButton::Config cfg;
        cfg.accentId   = "scaler";
        cfg.showFollow = true;
        cfg.rootNames  = SattariScalerAudioProcessor::rootChoices();
        cfg.scaleNames = SattariScalerAudioProcessor::scaleChoices();
        cfg.hint       = "click: follow song key / set key by hand";
        keyButton.setConfig (cfg);
    }
    keyButton.onLinkFollow = [this] { setKeyParam ("followKey", 1.0f); };
    keyButton.onManualKey  = [this] (int root, int scaleIdx)
    {
        setKeyParam ("followKey", 0.0f);
        setKeyParam ("root",  (float) root);
        setKeyParam ("scale", (float) scaleIdx);
    };
    addAndMakeVisible (keyButton);

    chord.addItemList (SattariScalerAudioProcessor::chordChoices(), 1);
    addAndMakeVisible (chord);
    chordAtt = std::make_unique<ComboAttachment> (processor.parameters(), "chord", chord);
    chordCap.setText ("CHORD", juce::dontSendNotification);
    chordCap.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    chordCap.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    addAndMakeVisible (chordCap);

    octave.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    octave.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    addAndMakeVisible (octave);
    octaveAtt = std::make_unique<SliderAttachment> (processor.parameters(), "octave", octave);
    octaveCap.setText ("OCTAVE", juce::dontSendNotification);
    octaveCap.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    octaveCap.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    octaveCap.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (octaveCap);

    startTimerHz (20);
}

SattariScalerAudioProcessorEditor::~SattariScalerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariScalerAudioProcessorEditor::setKeyParam (const juce::String& id, float value)
{
    if (auto* pp = processor.parameters().getParameter (id))
    {
        pp->beginChangeGesture();
        pp->setValueNotifyingHost (pp->convertTo0to1 (value));
        pp->endChangeGesture();
    }
}

void SattariScalerAudioProcessorEditor::timerCallback()
{
    const auto k = processor.currentKeyState();
    keyButton.setState (k.following ? sattari::ui::KeyButton::Mode::Follow
                                    : sattari::ui::KeyButton::Mode::Manual,
                        k.root, k.scale);
}

void SattariScalerAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariScalerAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (18);
    title.setBounds (b.removeFromTop (34));
    subtitle.setBounds (b.removeFromTop (20));
    b.removeFromTop (18);

    keyButton.setBounds (b.removeFromTop (56).removeFromLeft (320));
    b.removeFromTop (18);

    auto row = b.removeFromTop (90);
    auto cc = row.removeFromLeft (200);
    chordCap.setBounds (cc.removeFromTop (16));
    chord.setBounds (cc.removeFromTop (30));
    row.removeFromLeft (24);
    auto oc = row.removeFromLeft (90);
    octaveCap.setBounds (oc.removeFromBottom (16));
    octave.setBounds (oc);
}
