#include "PluginEditor.h"

namespace br = sattari::brand;

SattariVoxSynthAudioProcessorEditor::SattariVoxSynthAudioProcessorEditor (SattariVoxSynthAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    title.setText ("VoxSynth", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, juce::Colour (br::kTextPrimary));
    addAndMakeVisible (title);
    subtitle.setText (juce::String::fromUTF8 ("voice \xe2\x86\x92 instrument"), juce::dontSendNotification);
    subtitle.setColour (juce::Label::textColourId, juce::Colour (br::kTextMuted));
    addAndMakeVisible (subtitle);

    readout.setJustificationType (juce::Justification::centred);
    readout.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    readout.setColour (juce::Label::textColourId, juce::Colour (br::kAccent));
    addAndMakeVisible (readout);

    addAndMakeVisible (sessionHud);

    auto combo = [this] (juce::ComboBox& b, const juce::StringArray& items, const juce::String& id, std::unique_ptr<CA>& att)
    {
        b.addItemList (items, 1);
        addAndMakeVisible (b);
        att = std::make_unique<CA> (processor.parameters(), id, b);
    };
    combo (modeBox, { "Synth", "MIDI" }, "mode", modeAtt);
    combo (osc1Box, { "Sine", "Saw", "Square", "Triangle" }, "osc1", osc1Att);
    combo (osc2Box, { "Sine", "Saw", "Square", "Triangle" }, "osc2", osc2Att);

    addAndMakeVisible (percBtn);
    percBtn.setColour (juce::ToggleButton::textColourId, juce::Colour (br::kTextMuted));
    percAtt = std::make_unique<BA> (processor.parameters(), "percussion", percBtn);

    knob (mix, mixC, "Mix", "mix", mixA);
    knob (detune, detuneC, "Detune", "detune", detuneA);
    knob (glide, glideC, "Glide", "glide", glideA);
    knob (cutoff, cutoffC, "Cutoff", "cutoff", cutoffA);
    knob (filtAmt, filtAmtC, juce::String::fromUTF8 ("Vowel \xe2\x86\x92 Filter"), "filtamt", filtAmtA);
    knob (reso, resoC, "Reso", "reso", resoA);
    knob (quantize, quantizeC, "Quantize", "quantize", quantizeA);
    knob (vocoder, vocoderC, "Vocoder", "vocoder", vocoderA);
    knob (level, levelC, "Level", "level", levelA);

    setSize (620, 300);
    startTimerHz (30);
}

SattariVoxSynthAudioProcessorEditor::~SattariVoxSynthAudioProcessorEditor() { setLookAndFeel (nullptr); }

void SattariVoxSynthAudioProcessorEditor::knob (juce::Slider& s, juce::Label& c, const juce::String& name,
                                                const juce::String& id, std::unique_ptr<SA>& att)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 14);
    addAndMakeVisible (s);
    c.setText (name, juce::dontSendNotification);
    c.setJustificationType (juce::Justification::centred);
    c.setFont (juce::Font (juce::FontOptions (11.0f)));
    c.setColour (juce::Label::textColourId, juce::Colour (br::kTextMuted));
    addAndMakeVisible (c);
    att = std::make_unique<SA> (processor.parameters(), id, s);
}

void SattariVoxSynthAudioProcessorEditor::timerCallback()
{
    const auto r = processor.getLiveReading();
    static const char* N[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    if (r.voiced && r.midiNote >= 0)
    {
        const int oct = r.midiNote / 12 - 1;
        readout.setText (juce::String (N[r.midiNote % 12]) + juce::String (oct)
                         + "   " + (r.cents >= 0 ? "+" : "") + juce::String ((int) r.cents) + juce::String::fromUTF8 (" \xc2\xa2"),
                         juce::dontSendNotification);
        glow = 1.0f;
    }
    else { readout.setText (juce::String::fromUTF8 ("\xe2\x80\x94 sing / hum / beatbox \xe2\x80\x94"), juce::dontSendNotification); glow *= 0.9f; }
    repaint (0, 44, getWidth(), 26);
}

void SattariVoxSynthAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (br::kPanel));
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    g.setColour (juce::Colour (br::kPanelDeep));
    g.fillRoundedRectangle (r, 10.0f);
    auto strip = juce::Rectangle<float> (10.0f, 44.0f, (float) getWidth() - 20.0f, 26.0f);
    g.setColour (juce::Colour (br::kPanelRaised));
    g.fillRoundedRectangle (strip, 6.0f);
}

void SattariVoxSynthAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto top = r.removeFromTop (30);
    title.setBounds (top.removeFromLeft (110));
    subtitle.setBounds (top.removeFromLeft (110).withTrimmedTop (7));
    modeBox.setBounds (top.removeFromRight (76));
    top.removeFromRight (6);
    sessionHud.setBounds (top.removeFromRight (150).withSizeKeepingCentre (150, 22));

    readout.setBounds (r.removeFromTop (30));
    r.removeFromTop (8);

    auto oscRow = r.removeFromTop (24);
    osc1Box.setBounds (oscRow.removeFromLeft (120)); oscRow.removeFromLeft (8);
    osc2Box.setBounds (oscRow.removeFromLeft (120));
    percBtn.setBounds (oscRow.removeFromRight (120));
    r.removeFromTop (6);

    juce::Slider* ks[] = { &mix, &detune, &glide, &cutoff, &filtAmt, &reso, &quantize, &vocoder, &level };
    juce::Label*  cs[] = { &mixC, &detuneC, &glideC, &cutoffC, &filtAmtC, &resoC, &quantizeC, &vocoderC, &levelC };
    const int n = 9, w = r.getWidth() / n;
    for (int i = 0; i < n; ++i)
    {
        auto cell = r.removeFromLeft (w);
        cs[i]->setBounds (cell.removeFromBottom (14));
        ks[i]->setBounds (cell.reduced (2));
    }
}
