#include "PluginEditor.h"

SattariShimmerAudioProcessorEditor::SattariShimmerAudioProcessorEditor (SattariShimmerAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (700, 330);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Shimmer");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Pitch-shifted shimmer reverb");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    // Interval selector (+12 / +7 / +5).
    interval.addItemList (SattariShimmerAudioProcessor::intervalChoices(), 1);
    addAndMakeVisible (interval);
    intervalAtt = std::make_unique<CA> (processor.parameters(), "interval", interval);
    intervalCap.setText ("INTERVAL", juce::dontSendNotification);
    intervalCap.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    intervalCap.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    intervalCap.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (intervalCap);

    // Key Snap control (shared KeyButton). Manual key sets root/scale + turns
    // snap ON; "follow" turns snap ON (it will then track the bus).
    {
        sattari::ui::KeyButton::Config cfg;
        cfg.accentId   = "shimmer";
        cfg.showFollow = true;
        cfg.rootNames  = SattariShimmerAudioProcessor::rootChoices();
        cfg.scaleNames = SattariShimmerAudioProcessor::scaleChoices();
        cfg.followLabel = "Key Snap: follow song key  (from Sattari Key)";
        cfg.hint       = "key snap: follow song key / set key by hand";
        keyButton.setConfig (cfg);
    }
    keyButton.onLinkFollow = [this] { setKeyParam ("keysnap", 1.0f); };
    keyButton.onManualKey  = [this] (int root, int scaleIdx)
    {
        setKeyParam ("keysnap", 1.0f);
        setKeyParam ("root",  (float) root);
        setKeyParam ("scale", (float) scaleIdx);
    };
    addAndMakeVisible (keyButton);

    addKnob ("size",     "SIZE");
    addKnob ("shimmer",  "SHIMMER");
    addKnob ("damp",     "TONE");
    addKnob ("predelay", "PRE-DLY", " ms");
    addKnob ("width",    "WIDTH");
    addKnob ("mix",      "MIX");

    startTimerHz (20);
}

SattariShimmerAudioProcessorEditor::~SattariShimmerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariShimmerAudioProcessorEditor::setKeyParam (const juce::String& id, float value)
{
    if (auto* pp = processor.parameters().getParameter (id))
    {
        pp->beginChangeGesture();
        pp->setValueNotifyingHost (pp->convertTo0to1 (value));
        pp->endChangeGesture();
    }
}

void SattariShimmerAudioProcessorEditor::addKnob (const juce::String& id, const juce::String& cap, const juce::String& suffix)
{
    auto* s = new juce::Slider();
    s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 15);
    s->setTextValueSuffix (suffix);
    addAndMakeVisible (s); knobs.add (s);
    auto* c = new juce::Label();
    c->setText (cap, juce::dontSendNotification);
    c->setFont (juce::FontOptions (9.5f, juce::Font::bold));
    c->setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    c->setJustificationType (juce::Justification::centred);
    addAndMakeVisible (c); caps.add (c);
    atts.push_back (std::make_unique<SA> (processor.parameters(), id, *s));
}

void SattariShimmerAudioProcessorEditor::timerCallback()
{
    const auto k = processor.currentKeyState();
    keyButton.setState (k.snapping ? sattari::ui::KeyButton::Mode::Follow
                                   : sattari::ui::KeyButton::Mode::Manual,
                        k.root, k.scale);
}

void SattariShimmerAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariShimmerAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (16);
    title.setBounds (b.removeFromTop (34));
    subtitle.setBounds (b.removeFromTop (20));
    b.removeFromTop (12);

    // Top row: interval selector on the left, Key Snap button on the right.
    auto top = b.removeFromTop (60);
    auto ivCell = top.removeFromLeft (150);
    intervalCap.setBounds (ivCell.removeFromTop (14));
    interval.setBounds (ivCell.withSizeKeepingCentre (ivCell.getWidth() - 6, 30));
    top.removeFromLeft (16);
    keyButton.setBounds (top.removeFromLeft (330));

    b.removeFromTop (14);

    // Bottom row: 6 knobs.
    auto row = b.removeFromTop (120);
    const int w = row.getWidth() / 6;
    auto place = [] (juce::Slider* s, juce::Label* c, juce::Rectangle<int> cell)
    { c->setBounds (cell.removeFromBottom (15)); s->setBounds (cell.reduced (4, 2)); };
    for (int i = 0; i < knobs.size(); ++i)
        place (knobs[i], caps[i], row.removeFromLeft (w));
}
