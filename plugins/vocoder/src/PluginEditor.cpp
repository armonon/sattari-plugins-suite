#include "PluginEditor.h"

SattariVocoderAudioProcessorEditor::SattariVocoderAudioProcessorEditor (SattariVocoderAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (720, 400);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Vocoder");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Vocal vocoder — carrier auto-voices your key");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    {
        sattari::ui::KeyButton::Config cfg;
        cfg.accentId   = "vocoder";
        cfg.showFollow = true;
        cfg.rootNames  = SattariVocoderAudioProcessor::rootChoices();
        cfg.scaleNames = SattariVocoderAudioProcessor::scaleChoices();
        cfg.hint       = "carrier key — follow song key / set by hand / or play MIDI";
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

    addCombo ("wave",    "CARRIER", SattariVocoderAudioProcessor::waveChoices());
    addCombo ("voicing", "VOICING", SattariVocoderAudioProcessor::voicingChoices());

    addKnob ("bands",    "BANDS");
    addKnob ("octave",   "OCTAVE");
    addKnob ("formant",  "FORMANT", " st");
    addKnob ("unvoiced", "SIBILANCE");
    addKnob ("attack",   "ATTACK",  " ms");
    addKnob ("release",  "RELEASE", " ms");
    addKnob ("mix",      "DRY/WET");
    addKnob ("output",   "OUT",     " dB");

    startTimerHz (20);
}

SattariVocoderAudioProcessorEditor::~SattariVocoderAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

juce::Slider& SattariVocoderAudioProcessorEditor::addKnob (const juce::String& id, const juce::String& caption,
                                                           const juce::String& suffix)
{
    auto* s = new juce::Slider();
    s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 15);
    s->setTextValueSuffix (suffix);
    addAndMakeVisible (s);
    knobs.add (s);

    auto* c = new juce::Label();
    c->setText (caption, juce::dontSendNotification);
    c->setFont (sattari::brand::fontOptions (9.5f, juce::Font::bold));
    c->setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    c->setJustificationType (juce::Justification::centred);
    addAndMakeVisible (c);
    caps.add (c);

    knobAtts.push_back (std::make_unique<SA> (processor.parameters(), id, *s));
    return *s;
}

juce::ComboBox& SattariVocoderAudioProcessorEditor::addCombo (const juce::String& id, const juce::String& caption,
                                                              const juce::StringArray& items)
{
    auto* cb = new juce::ComboBox();
    cb->addItemList (items, 1);
    addAndMakeVisible (cb);
    combos.add (cb);

    auto* c = new juce::Label();
    c->setText (caption, juce::dontSendNotification);
    c->setFont (sattari::brand::fontOptions (9.5f, juce::Font::bold));
    c->setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    c->setJustificationType (juce::Justification::centred);
    addAndMakeVisible (c);
    caps.add (c);

    comboAtts.push_back (std::make_unique<CA> (processor.parameters(), id, *cb));
    return *cb;
}

void SattariVocoderAudioProcessorEditor::setKeyParam (const juce::String& id, float value)
{
    if (auto* pp = processor.parameters().getParameter (id))
    {
        pp->beginChangeGesture();
        pp->setValueNotifyingHost (pp->convertTo0to1 (value));
        pp->endChangeGesture();
    }
}

void SattariVocoderAudioProcessorEditor::timerCallback()
{
    const auto k = processor.currentKeyState();
    keyButton.setState (k.following ? sattari::ui::KeyButton::Mode::Follow
                                    : sattari::ui::KeyButton::Mode::Manual,
                        k.root, k.scale);
}

void SattariVocoderAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariVocoderAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (18);
    title.setBounds (b.removeFromTop (32));
    subtitle.setBounds (b.removeFromTop (20));
    b.removeFromTop (12);

    // Top row: key button + the two carrier combos.
    auto top = b.removeFromTop (60);
    keyButton.setBounds (top.removeFromLeft (330));
    top.removeFromLeft (18);
    // caps[0]/[1] belong to the two combos (added before any knob captions).
    for (int i = 0; i < combos.size(); ++i)
    {
        auto cell = top.removeFromLeft (150);
        caps[i]->setBounds (cell.removeFromTop (14));
        combos[i]->setBounds (cell.removeFromTop (28).reduced (2, 0));
        top.removeFromLeft (8);
    }

    b.removeFromTop (16);

    // Knob grid (8 knobs, one row of 8, wraps if narrow).
    const int cols = 8;
    const int cw = b.getWidth() / cols;
    const int rh = 116;
    for (int i = 0; i < knobs.size(); ++i)
    {
        const int r = i / cols, col = i % cols;
        auto cell = juce::Rectangle<int> (b.getX() + col * cw, b.getY() + r * rh, cw, rh);
        // knob captions are stored after the two combo captions.
        caps[combos.size() + i]->setBounds (cell.removeFromBottom (15));
        knobs[i]->setBounds (cell.reduced (4, 2));
    }
}
